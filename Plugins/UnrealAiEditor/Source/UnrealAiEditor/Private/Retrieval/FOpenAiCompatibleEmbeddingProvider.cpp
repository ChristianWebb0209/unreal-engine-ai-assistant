#include "Retrieval/FOpenAiCompatibleEmbeddingProvider.h"

#include "Harness/UnrealAiHarnessTpmThrottle.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Async/Async.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/UnrealAiWaitTimePolicy.h"
#include "Misc/ScopeLock.h"

namespace
{
	struct FEmbedGameThreadSync
	{
		FEvent* DoneEvent = nullptr;

		FEmbedGameThreadSync()
		{
			DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
		}

		~FEmbedGameThreadSync()
		{
			if (DoneEvent)
			{
				FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
				DoneEvent = nullptr;
			}
		}

		void SignalDone()
		{
			if (DoneEvent)
			{
				DoneEvent->Trigger();
			}
		}

		FEmbedGameThreadSync(const FEmbedGameThreadSync&) = delete;
		FEmbedGameThreadSync& operator=(const FEmbedGameThreadSync&) = delete;
	};

	static bool WaitForEventWhilePumpingHttpAndGameThread(FEvent* Event, uint32 WaitMs)
	{
		if (!Event)
		{
			return false;
		}
		// If not on game thread, plain wait is fine.
		if (!IsInGameThread())
		{
			return Event->Wait(WaitMs);
		}
		const double DeadlineSec = FPlatformTime::Seconds() + static_cast<double>(WaitMs) / 1000.0;
		while (FPlatformTime::Seconds() < DeadlineSec)
		{
			if (Event->Wait(0u))
			{
				return true;
			}
			FHttpModule::Get().GetHttpManager().Tick(0.f);
			FPlatformProcess::SleepNoStats(0.001f);
		}
		return Event->Wait(0u);
	}
}

FOpenAiCompatibleEmbeddingProvider::FOpenAiCompatibleEmbeddingProvider(FUnrealAiModelProfileRegistry* InProfiles)
	: Profiles(InProfiles)
{
}

bool FOpenAiCompatibleEmbeddingProvider::EmbedOne(
	const FString& ModelId,
	const FUnrealAiEmbeddingRequest& Request,
	FUnrealAiEmbeddingResponse& OutResponse)
{
	if (!IsInGameThread())
	{
		const TSharedPtr<FEmbedGameThreadSync> Sync = MakeShared<FEmbedGameThreadSync>();
		if (!Sync->DoneEvent)
		{
			OutResponse.Error = TEXT("Failed to allocate sync event for game-thread embedding dispatch.");
			return false;
		}

		// Copy everything the game-thread task needs; capturing references to the caller stack is UB
		// once the waiter returns or if the task is reordered relative to unrelated pumping.
		const FString ModelIdCopy(ModelId);
		const FUnrealAiEmbeddingRequest RequestCopy(Request);
		const TSharedPtr<FUnrealAiEmbeddingResponse> GameThreadResponse = MakeShared<FUnrealAiEmbeddingResponse>();
		struct FSyncOutcome
		{
			bool bOk = false;
		};
		const TSharedPtr<FSyncOutcome> Outcome = MakeShared<FSyncOutcome>();

		AsyncTask(ENamedThreads::GameThread, [this, ModelIdCopy, RequestCopy, GameThreadResponse, Outcome, Sync]()
		{
			struct FAlwaysSignal
			{
				TSharedPtr<FEmbedGameThreadSync> S;
				~FAlwaysSignal()
				{
					if (S.IsValid())
					{
						S->SignalDone();
					}
				}
			} AlwaysSignal{Sync};

			Outcome->bOk = EmbedOne_OnGameThread(ModelIdCopy, RequestCopy, *GameThreadResponse);
		});

		// If Wait times out, the calling thread must not return Sync->DoneEvent to the pool while the
		// game-thread task may still run; FEmbedGameThreadSync lifetime is tied to this TSharedPtr and
		// the AsyncTask capture so the event is pooled only after Trigger (or last ref drop).
		const bool bSignaled = Sync->DoneEvent->Wait(static_cast<uint32>(FMath::Max(1000.0f, (UnrealAiWaitTime::EmbeddingHttpTimeoutSec * 1000.0f) + 2000.0f)));
		if (!bSignaled)
		{
			OutResponse.Error = TEXT("Timed out waiting for game-thread embedding dispatch.");
			return false;
		}
		OutResponse = MoveTemp(*GameThreadResponse);
		return Outcome->bOk;
	}

	return EmbedOne_OnGameThread(ModelId, Request, OutResponse);
}

bool FOpenAiCompatibleEmbeddingProvider::EmbedOne_OnGameThread(
	const FString& ModelId,
	const FUnrealAiEmbeddingRequest& Request,
	FUnrealAiEmbeddingResponse& OutResponse)
{
	if (!Profiles)
	{
		OutResponse.Error = TEXT("Embedding provider missing model profile registry.");
		return false;
	}

	FString BaseUrl;
	FString ApiKey;
	// Resolve API credentials for the embedding model path, not the default chat model.
	// This prevents accidental provider/base-url mismatch when chat and embeddings differ.
	if (!Profiles->TryResolveApiForModel(ModelId, BaseUrl, ApiKey) || ApiKey.IsEmpty())
	{
		OutResponse.Error = TEXT("No API key configured for embeddings.");
		return false;
	}
	while (!BaseUrl.IsEmpty() && BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1, EAllowShrinking::No);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), ModelId);
	TArray<TSharedPtr<FJsonValue>> Inputs;
	Inputs.Add(MakeShared<FJsonValueString>(Request.InputText));
	Root->SetArrayField(TEXT("input"), Inputs);

	FString Body;
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	}

	const FString Url = BaseUrl + TEXT("/embeddings");
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Url);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(Body);

	// Embeddings should be quick for small query text.
	// Keep this fail-fast so a bad network path does not consume most of a harness turn.
	const float TimeoutSec = UnrealAiWaitTime::EmbeddingHttpTimeoutSec;
	HttpRequest->SetTimeout(TimeoutSec);

	UnrealAiHarnessTpmThrottle::MaybeWaitBeforeEmbeddingRequest(Request.InputText.Len());

	struct FEmbeddingRequestState
	{
		FCriticalSection Mutex;
		bool bDone = false;
		bool bOk = false;
		int32 StatusCode = 0;
		FString ResponseBody;
		FEvent* DoneEvent = nullptr;
		bool bEventArmed = true;
	};
	TSharedRef<FEmbeddingRequestState, ESPMode::ThreadSafe> State = MakeShared<FEmbeddingRequestState, ESPMode::ThreadSafe>();
	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
	if (!DoneEvent)
	{
		OutResponse.Error = TEXT("Failed to allocate sync event for embeddings request.");
		return false;
	}
	State->DoneEvent = DoneEvent;
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[State](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedOk)
		{
			const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
			FEvent* EventToTrigger = nullptr;
			{
				FScopeLock Lock(&State->Mutex);
				State->bOk = bConnectedOk && Response.IsValid() && Status >= 200 && Status < 300;
				State->StatusCode = Status;
				State->ResponseBody = Response.IsValid() ? Response->GetContentAsString() : FString();
				State->bDone = true;
				if (State->bEventArmed)
				{
					EventToTrigger = State->DoneEvent;
				}
			}
			if (EventToTrigger)
			{
				EventToTrigger->Trigger();
			}
		});

	if (!HttpRequest->ProcessRequest())
	{
		OutResponse.Error = TEXT("Failed to start embeddings request.");
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
		return false;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	// Keep wait margin small so timeout paths return quickly to lexical fallback.
	const uint32 WaitMs = static_cast<uint32>(FMath::Max(1000.0f, (TimeoutSec * 1000.0f) + 100.0f));
	const bool bSignaled = WaitForEventWhilePumpingHttpAndGameThread(DoneEvent, WaitMs);
	const double ElapsedMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	UE_LOG(
		LogTemp,
		Display,
		TEXT("Retrieval embeddings HTTP model=%s timeout=%.2fs waited_ms=%.0f input_chars=%d url=%s"),
		*ModelId,
		TimeoutSec,
		ElapsedMs,
		Request.InputText.Len(),
		*Url);
	if (!bSignaled)
	{
		HttpRequest->CancelRequest();
		HttpRequest->OnProcessRequestComplete().Unbind();
		{
			FScopeLock Lock(&State->Mutex);
			State->bEventArmed = false;
			State->DoneEvent = nullptr;
		}
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
		OutResponse.Error = FString::Printf(
			TEXT("Embeddings HTTP request timed out after %.2fs (waited %.0f ms)."),
			TimeoutSec,
			ElapsedMs);
		return false;
	}
	bool bDone = false;
	bool bOk = false;
	int32 StatusCode = 0;
	FString ResponseBody;
	{
		FScopeLock Lock(&State->Mutex);
		bDone = State->bDone;
		bOk = State->bOk;
		StatusCode = State->StatusCode;
		ResponseBody = State->ResponseBody;
		State->bEventArmed = false;
		State->DoneEvent = nullptr;
	}
	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
	if (!bDone || !bOk)
	{
		const FString BodyPreview = ResponseBody.Left(240).ReplaceCharWithEscapedChar();
		OutResponse.Error = FString::Printf(
			TEXT("Embeddings HTTP request failed (status=%d, body=%s)"),
			StatusCode,
			*BodyPreview);
		return false;
	}

	TSharedPtr<FJsonObject> ResponseJson;
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
		if (!FJsonSerializer::Deserialize(Reader, ResponseJson) || !ResponseJson.IsValid())
		{
			OutResponse.Error = TEXT("Embeddings response JSON parse failed.");
			return false;
		}
	}
	{
		const TSharedPtr<FJsonObject>* UsageObj = nullptr;
		if (ResponseJson->TryGetObjectField(TEXT("usage"), UsageObj) && UsageObj && (*UsageObj).IsValid())
		{
			double TotalTok = 0.0;
			double PromptTok = 0.0;
			if ((*UsageObj)->TryGetNumberField(TEXT("total_tokens"), TotalTok) && TotalTok > 0.0)
			{
				UnrealAiHarnessTpmThrottle::RecordEmbeddingTokens(static_cast<int32>(TotalTok));
			}
			else if ((*UsageObj)->TryGetNumberField(TEXT("prompt_tokens"), PromptTok) && PromptTok > 0.0)
			{
				UnrealAiHarnessTpmThrottle::RecordEmbeddingTokens(static_cast<int32>(PromptTok));
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* DataArray = nullptr;
	if (!ResponseJson->TryGetArrayField(TEXT("data"), DataArray) || !DataArray || DataArray->Num() == 0)
	{
		OutResponse.Error = TEXT("Embeddings response missing data.");
		return false;
	}
	const TSharedPtr<FJsonObject>* FirstObj = nullptr;
	if (!(*DataArray)[0].IsValid() || !(*DataArray)[0]->TryGetObject(FirstObj) || !FirstObj || !(*FirstObj).IsValid())
	{
		OutResponse.Error = TEXT("Embeddings response data[0] invalid.");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* EmbeddingArray = nullptr;
	if (!(*FirstObj)->TryGetArrayField(TEXT("embedding"), EmbeddingArray) || !EmbeddingArray)
	{
		OutResponse.Error = TEXT("Embeddings response missing embedding array.");
		return false;
	}
	OutResponse.Vector.Reset();
	OutResponse.Vector.Reserve(EmbeddingArray->Num());
	for (const TSharedPtr<FJsonValue>& Value : *EmbeddingArray)
	{
		const double Number = Value.IsValid() ? Value->AsNumber() : 0.0;
		OutResponse.Vector.Add(static_cast<float>(Number));
	}
	return OutResponse.Vector.Num() > 0;
}
