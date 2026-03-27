#include "Transport/FOpenAiCompatibleHttpTransport.h"

#include "Harness/UnrealAiConversationJson.h"
#include "HttpModule.h"
#include "Interfaces/IHttpBase.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DefaultValueHelper.h"

namespace OpenAiTransportUtil
{
	static void EmitError(const FUnrealAiLlmStreamCallback& OnEvent, const FString& Msg)
	{
		FUnrealAiLlmStreamEvent Ev;
		Ev.Type = EUnrealAiLlmStreamEventType::Error;
		Ev.ErrorMessage = Msg;
		OnEvent.ExecuteIfBound(Ev);
	}

	static void ParseUsage(const TSharedPtr<FJsonObject>& Root, FUnrealAiTokenUsage& OutUsage)
	{
		const TSharedPtr<FJsonObject>* U = nullptr;
		if (!Root->TryGetObjectField(TEXT("usage"), U) || !U->IsValid())
		{
			return;
		}
		double Pt = 0, Ct = 0, Tt = 0;
		if ((*U)->TryGetNumberField(TEXT("prompt_tokens"), Pt)
			|| (*U)->TryGetNumberField(TEXT("input_tokens"), Pt)
			|| (*U)->TryGetNumberField(TEXT("prompt_token_count"), Pt))
		{
			OutUsage.PromptTokens = static_cast<int32>(Pt);
		}
		if ((*U)->TryGetNumberField(TEXT("completion_tokens"), Ct)
			|| (*U)->TryGetNumberField(TEXT("output_tokens"), Ct)
			|| (*U)->TryGetNumberField(TEXT("candidates_token_count"), Ct))
		{
			OutUsage.CompletionTokens = static_cast<int32>(Ct);
		}
		if ((*U)->TryGetNumberField(TEXT("total_tokens"), Tt) || (*U)->TryGetNumberField(TEXT("total_token_count"), Tt))
		{
			OutUsage.TotalTokens = static_cast<int32>(Tt);
		}
	}

	static void ParseNonStreamBody(const FString& Body, const FUnrealAiLlmStreamCallback& OnEvent)
	{
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Body);
		if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
		{
			EmitError(OnEvent, TEXT("Invalid JSON from model"));
			return;
		}
		const TSharedPtr<FJsonObject>* Err = nullptr;
		if (Root->TryGetObjectField(TEXT("error"), Err) && Err->IsValid())
		{
			FString Msg;
			(*Err)->TryGetStringField(TEXT("message"), Msg);
			if (Msg.IsEmpty())
			{
				Msg = TEXT("Model API error");
			}
			EmitError(OnEvent, Msg);
			return;
		}
		const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
		if (!Root->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
		{
			EmitError(OnEvent, TEXT("No choices in model response"));
			return;
		}
		const TSharedPtr<FJsonObject>* Choice0 = nullptr;
		if (!(*Choices)[0]->TryGetObject(Choice0) || !Choice0->IsValid())
		{
			EmitError(OnEvent, TEXT("Bad choice object"));
			return;
		}
		const TSharedPtr<FJsonObject>* MsgObj = nullptr;
		if (!(*Choice0)->TryGetObjectField(TEXT("message"), MsgObj) || !MsgObj->IsValid())
		{
			EmitError(OnEvent, TEXT("No message in choice"));
			return;
		}
		FString Content;
		(*MsgObj)->TryGetStringField(TEXT("content"), Content);
		if (!Content.IsEmpty())
		{
			FUnrealAiLlmStreamEvent DeltaEv;
			DeltaEv.Type = EUnrealAiLlmStreamEventType::AssistantDelta;
			DeltaEv.DeltaText = Content;
			OnEvent.ExecuteIfBound(DeltaEv);
		}
		FString Reasoning;
		if (((*MsgObj)->TryGetStringField(TEXT("reasoning_content"), Reasoning) || (*MsgObj)->TryGetStringField(TEXT("reasoning"), Reasoning))
			&& !Reasoning.IsEmpty())
		{
			FUnrealAiLlmStreamEvent Th;
			Th.Type = EUnrealAiLlmStreamEventType::ThinkingDelta;
			Th.DeltaText = Reasoning;
			OnEvent.ExecuteIfBound(Th);
		}
		const TArray<TSharedPtr<FJsonValue>>* TcArr = nullptr;
		if ((*MsgObj)->TryGetArrayField(TEXT("tool_calls"), TcArr) && TcArr && TcArr->Num() > 0)
		{
			TArray<FUnrealAiToolCallSpec> ToolCalls;
			for (const TSharedPtr<FJsonValue>& V : *TcArr)
			{
				const TSharedPtr<FJsonObject>* Tco = nullptr;
				if (!V.IsValid() || !V->TryGetObject(Tco) || !Tco->IsValid())
				{
					continue;
				}
				FUnrealAiToolCallSpec Tc;
				(*Tco)->TryGetStringField(TEXT("id"), Tc.Id);
				const TSharedPtr<FJsonObject>* Fn = nullptr;
				if ((*Tco)->TryGetObjectField(TEXT("function"), Fn) && Fn->IsValid())
				{
					(*Fn)->TryGetStringField(TEXT("name"), Tc.Name);
					(*Fn)->TryGetStringField(TEXT("arguments"), Tc.ArgumentsJson);
				}
				ToolCalls.Add(MoveTemp(Tc));
			}
			if (ToolCalls.Num() > 0)
			{
				FUnrealAiLlmStreamEvent TcEv;
				TcEv.Type = EUnrealAiLlmStreamEventType::ToolCalls;
				TcEv.ToolCalls = MoveTemp(ToolCalls);
				OnEvent.ExecuteIfBound(TcEv);
			}
		}
		FString FinishReason;
		(*Choice0)->TryGetStringField(TEXT("finish_reason"), FinishReason);
		FUnrealAiLlmStreamEvent Fin;
		Fin.Type = EUnrealAiLlmStreamEventType::Finish;
		Fin.FinishReason = FinishReason.IsEmpty() ? TEXT("stop") : FinishReason;
		ParseUsage(Root, Fin.Usage);
		OnEvent.ExecuteIfBound(Fin);
	}

	static void ParseSseBody(const FString& Body, const FUnrealAiLlmStreamCallback& OnEvent)
	{
		TArray<FString> Lines;
		Body.ParseIntoArrayLines(Lines, false);
		FUnrealAiTokenUsage LastUsage;
		FString LastFinishReason;
		bool bEmittedFinish = false;

		auto EmitFinalFinish = [&OnEvent, &LastUsage, &LastFinishReason, &bEmittedFinish]()
		{
			if (bEmittedFinish)
			{
				return;
			}
			bEmittedFinish = true;
			FUnrealAiLlmStreamEvent Fin;
			Fin.Type = EUnrealAiLlmStreamEventType::Finish;
			Fin.FinishReason = LastFinishReason.IsEmpty() ? TEXT("stop") : LastFinishReason;
			Fin.Usage = LastUsage;
			OnEvent.ExecuteIfBound(Fin);
		};

		for (FString& Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line.IsEmpty())
			{
				continue;
			}
			if (!Line.StartsWith(TEXT("data:")))
			{
				continue;
			}
			FString Payload = Line.Mid(5).TrimStart();
			if (Payload == TEXT("[DONE]"))
			{
				EmitFinalFinish();
				continue;
			}
			TSharedPtr<FJsonObject> Chunk;
			TSharedRef<TJsonReader<>> JR = TJsonReaderFactory<>::Create(Payload);
			if (!FJsonSerializer::Deserialize(JR, Chunk) || !Chunk.IsValid())
			{
				continue;
			}
			// Usage may appear on its own chunk (e.g. stream_options.include_usage) before [DONE].
			ParseUsage(Chunk, LastUsage);
			const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
			if (!Chunk->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>* Choice0 = nullptr;
			if (!(*Choices)[0]->TryGetObject(Choice0) || !Choice0->IsValid())
			{
				continue;
			}
			const TSharedPtr<FJsonObject>* Delta = nullptr;
			if (!(*Choice0)->TryGetObjectField(TEXT("delta"), Delta) || !Delta->IsValid())
			{
				continue;
			}
			FString Part;
			if ((*Delta)->TryGetStringField(TEXT("content"), Part) && !Part.IsEmpty())
			{
				FUnrealAiLlmStreamEvent De;
				De.Type = EUnrealAiLlmStreamEventType::AssistantDelta;
				De.DeltaText = Part;
				OnEvent.ExecuteIfBound(De);
			}
			FString Rc;
			if (((*Delta)->TryGetStringField(TEXT("reasoning_content"), Rc) || (*Delta)->TryGetStringField(TEXT("reasoning"), Rc))
				&& !Rc.IsEmpty())
			{
				FUnrealAiLlmStreamEvent Th;
				Th.Type = EUnrealAiLlmStreamEventType::ThinkingDelta;
				Th.DeltaText = Rc;
				OnEvent.ExecuteIfBound(Th);
			}
			const TArray<TSharedPtr<FJsonValue>>* TcArr = nullptr;
			if ((*Delta)->TryGetArrayField(TEXT("tool_calls"), TcArr) && TcArr)
			{
				for (const TSharedPtr<FJsonValue>& V : *TcArr)
				{
					const TSharedPtr<FJsonObject>* Tco = nullptr;
					if (!V.IsValid() || !V->TryGetObject(Tco) || !Tco->IsValid())
					{
						continue;
					}
					FUnrealAiToolCallSpec Tc;
					(*Tco)->TryGetStringField(TEXT("id"), Tc.Id);
					double IdxNum = 0;
					if ((*Tco)->TryGetNumberField(TEXT("index"), IdxNum))
					{
						Tc.StreamMergeIndex = static_cast<int32>(IdxNum);
					}
					const TSharedPtr<FJsonObject>* Fn = nullptr;
					if ((*Tco)->TryGetObjectField(TEXT("function"), Fn) && Fn->IsValid())
					{
						(*Fn)->TryGetStringField(TEXT("name"), Tc.Name);
						FString Args;
						if ((*Fn)->TryGetStringField(TEXT("arguments"), Args))
						{
							Tc.ArgumentsJson += Args;
						}
					}
					if (Tc.StreamMergeIndex >= 0 || !Tc.Id.IsEmpty() || !Tc.Name.IsEmpty()
						|| !Tc.ArgumentsJson.IsEmpty())
					{
						FUnrealAiLlmStreamEvent TcEv;
						TcEv.Type = EUnrealAiLlmStreamEventType::ToolCalls;
						TcEv.ToolCalls.Add(Tc);
						OnEvent.ExecuteIfBound(TcEv);
					}
				}
			}
			FString FinishReason;
			if ((*Choice0)->TryGetStringField(TEXT("finish_reason"), FinishReason) && !FinishReason.IsEmpty())
			{
				// Defer Finish until [DONE] so usage from stream_options.include_usage is included.
				LastFinishReason = FinishReason;
			}
		}

		// Some proxies omit the final data: [DONE] line; still complete the harness turn.
		EmitFinalFinish();
	}

	static float ParseRetryAfterSecondsBestEffort(const FString& RespBody, const FString& RetryAfterHeader)
	{
		float HeaderSeconds = 0.0f;
		if (!RetryAfterHeader.IsEmpty())
		{
			// OpenAI-style proxies may send integer seconds.
			if (FDefaultValueHelper::ParseFloat(RetryAfterHeader, HeaderSeconds) && HeaderSeconds > 0.0f)
			{
				return FMath::Clamp(HeaderSeconds, 0.5f, 10.0f);
			}
		}

		// Example: "Please try again in 3.756s."
		const FString Needle = TEXT("Please try again in ");
		const int32 Idx = RespBody.Find(Needle, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (Idx >= 0)
		{
			const int32 Start = Idx + Needle.Len();
			int32 End = Start;
			while (End < RespBody.Len())
			{
				const TCHAR Ch = RespBody[End];
				if ((Ch >= '0' && Ch <= '9') || Ch == '.' || Ch == ',')
				{
					++End;
					continue;
				}
				break;
			}
			FString Num = RespBody.Mid(Start, End - Start);
			Num.ReplaceInline(TEXT(","), TEXT("."));
			float BodySeconds = 0.0f;
			if (FDefaultValueHelper::ParseFloat(Num, BodySeconds) && BodySeconds > 0.0f)
			{
				return FMath::Clamp(BodySeconds, 0.5f, 10.0f);
			}
		}

		// Fallback: small jittered wait.
		return 2.0f;
	}
}

void FOpenAiCompatibleHttpTransport::CancelActiveRequest()
{
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
}

void FOpenAiCompatibleHttpTransport::StreamChatCompletion(const FUnrealAiLlmRequest& Request, FUnrealAiLlmStreamCallback OnEvent)
{
	CancelActiveRequest();
	const FString Base = Request.ApiBaseUrl;
	const FString Key = Request.ApiKey.TrimStartAndEnd();
	if (Base.IsEmpty() || Key.IsEmpty())
	{
		OpenAiTransportUtil::EmitError(OnEvent, TEXT("API base URL or API key missing on request (configure settings / providers)"));
		return;
	}
	FString MessagesJson;
	if (!UnrealAiConversationJson::MessagesToChatCompletionsJsonArray(Request.Messages, MessagesJson))
	{
		OpenAiTransportUtil::EmitError(OnEvent, TEXT("Failed to serialize messages"));
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Request.ApiModelName);
	{
		TSharedRef<TJsonReader<>> MR = TJsonReaderFactory<>::Create(MessagesJson);
		TArray<TSharedPtr<FJsonValue>> MsgArr;
		if (!FJsonSerializer::Deserialize(MR, MsgArr))
		{
			OpenAiTransportUtil::EmitError(OnEvent, TEXT("Bad messages JSON"));
			return;
		}
		Body->SetArrayField(TEXT("messages"), MsgArr);
	}
	if (!Request.ToolsJsonArray.IsEmpty())
	{
		TSharedRef<TJsonReader<>> TR = TJsonReaderFactory<>::Create(Request.ToolsJsonArray);
		TArray<TSharedPtr<FJsonValue>> ToolsArr;
		if (FJsonSerializer::Deserialize(TR, ToolsArr))
		{
			Body->SetArrayField(TEXT("tools"), ToolsArr);
		}
	}
	Body->SetBoolField(TEXT("stream"), Request.bStream);
	if (Request.bStream)
	{
		// Many chat-completions APIs omit usage from stream chunks unless stream_options.include_usage (or equivalent) is set.
		TSharedPtr<FJsonObject> StreamOpts = MakeShared<FJsonObject>();
		StreamOpts->SetBoolField(TEXT("include_usage"), true);
		Body->SetObjectField(TEXT("stream_options"), StreamOpts);
	}
	Body->SetNumberField(TEXT("max_tokens"), Request.MaxOutputTokens);
	FString BodyStr;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&BodyStr);
	if (!FJsonSerializer::Serialize(Body.ToSharedRef(), W))
	{
		OpenAiTransportUtil::EmitError(OnEvent, TEXT("Failed to serialize request body"));
		return;
	}
	FString Url = Base;
	if (!Url.EndsWith(TEXT("/")))
	{
		Url += TEXT("/");
	}
	Url += TEXT("chat/completions");

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Url);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Key));
	HttpRequest->SetContentAsString(BodyStr);
	// Hard cap 30s: agent chat in-editor should complete quickly; longer waits usually mean wrong URL, blocked TLS, or huge payloads.
	// Override: UNREAL_AI_HTTP_REQUEST_TIMEOUT_SEC (clamped to 5-30). Same limit for streaming and non-streaming.
	float TimeoutSec = 30.0f;
	{
		const FString TEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HTTP_REQUEST_TIMEOUT_SEC"));
		if (!TEnv.IsEmpty())
		{
			float Parsed = FCString::Atof(*TEnv);
			if (Parsed < 5.0f)
			{
				Parsed = 5.0f;
			}
			if (Parsed > 30.0f)
			{
				Parsed = 30.0f;
			}
			TimeoutSec = Parsed;
		}
	}
	HttpRequest->SetTimeout(TimeoutSec);

	UE_LOG(LogTemp, Display,
		TEXT("UnrealAi HTTP: stream=%s timeout=%.0fs url=%s bearerLen=%d bodyChars=%d toolsChars=%d"),
		Request.bStream ? TEXT("yes") : TEXT("no"),
		TimeoutSec,
		*Url,
		Key.Len(),
		BodyStr.Len(),
		Request.ToolsJsonArray.Len());

	// Best-effort 429 backoff/retry: prevents single-turn failures when the org TPM bucket is briefly exhausted.
	// Keep small to avoid long hangs in-editor.
	const int32 MaxAttempts = 3;
	TSharedRef<int32, ESPMode::ThreadSafe> Attempt = MakeShared<int32, ESPMode::ThreadSafe>(0);

	TFunction<void()> SendAttempt;
	SendAttempt = [this, OnEvent, bStream = Request.bStream, Url, Key, BodyStr, TimeoutSec, Attempt, MaxAttempts, &SendAttempt]()
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest2 = FHttpModule::Get().CreateRequest();
		HttpRequest2->SetURL(Url);
		HttpRequest2->SetVerb(TEXT("POST"));
		HttpRequest2->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		HttpRequest2->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Key));
		HttpRequest2->SetContentAsString(BodyStr);
		HttpRequest2->SetTimeout(TimeoutSec);

		ActiveRequest = HttpRequest2;
		HttpRequest2->OnProcessRequestComplete().BindLambda(
			[this, OnEvent, bStream, Url, Attempt, MaxAttempts, &SendAttempt](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
			{
				ActiveRequest.Reset();
				if (!bOk)
				{
					FString Detail = TEXT("HTTP request failed (transport did not complete successfully)");
					if (Req.IsValid())
					{
						Detail = FString::Printf(
							TEXT("HTTP request failed: %s (%s). URL: %s"),
							EHttpRequestStatus::ToString(Req->GetStatus()),
							LexToString(Req->GetFailureReason()),
							*Req->GetURL());
					}
					else if (Resp.IsValid())
					{
						Detail = FString::Printf(
							TEXT("HTTP request failed: %s (%s)"),
							EHttpRequestStatus::ToString(Resp->GetStatus()),
							LexToString(Resp->GetFailureReason()));
					}
					OpenAiTransportUtil::EmitError(OnEvent, Detail);
					return;
				}
				if (!Resp.IsValid())
				{
					OpenAiTransportUtil::EmitError(OnEvent, TEXT("HTTP request failed: no response (check API URL, key, firewall, proxy)"));
					return;
				}

				const int32 Code = Resp->GetResponseCode();
				const FString RespBody = Resp->GetContentAsString();
				if (Code == 429 && (*Attempt) < (MaxAttempts - 1))
				{
					const FString RetryAfterHeader = Resp->GetHeader(TEXT("Retry-After"));
					const float WaitSec = OpenAiTransportUtil::ParseRetryAfterSecondsBestEffort(RespBody, RetryAfterHeader);
					UE_LOG(LogTemp, Warning, TEXT("UnrealAi HTTP 429; backing off %.2fs then retrying (attempt %d/%d)."), WaitSec, (*Attempt) + 1, MaxAttempts);
					++(*Attempt);
					FPlatformProcess::Sleep(WaitSec);
					SendAttempt();
					return;
				}

				if (Code < 200 || Code >= 300)
				{
					OpenAiTransportUtil::EmitError(OnEvent, FString::Printf(TEXT("HTTP %d: %s"), Code, *RespBody.Left(500)));
					return;
				}

				if (bStream)
				{
					OpenAiTransportUtil::ParseSseBody(RespBody, OnEvent);
				}
				else
				{
					OpenAiTransportUtil::ParseNonStreamBody(RespBody, OnEvent);
				}
			});

		const bool bStarted2 = HttpRequest2->ProcessRequest();
		if (!bStarted2)
		{
			ActiveRequest.Reset();
			OpenAiTransportUtil::EmitError(
				OnEvent,
				FString::Printf(
					TEXT("HTTP request failed to start (URL=%s). Check API URL, TLS/proxy/firewall, and provider settings."),
					*Url));
		}
	};

	// Kick off initial attempt.
	SendAttempt();
}
