#include "Retrieval/FOpenAiCompatibleEmbeddingProvider.h"

#include "Retrieval/UnrealAiOpenAiBatchEmbeddingsParse.h"
#include "Harness/UnrealAiHarnessTpmThrottle.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Async/Async.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
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

	static bool WaitEmbeddingPollHttpOnCallerThread(FEvent* Event, uint32 WaitMs)
	{
		if (!Event)
		{
			return false;
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

	static int32 TotalInputChars(const TArray<FString>& InputTexts)
	{
		int64 Sum = 0;
		for (const FString& T : InputTexts)
		{
			Sum += static_cast<int64>(T.Len());
			if (Sum > INT32_MAX)
			{
				return INT32_MAX;
			}
		}
		return static_cast<int32>(Sum);
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
	TArray<FString> One;
	One.Add(Request.InputText);

	if (Request.bBackgroundIndexer)
	{
		TArray<TArray<float>> BatchOut;
		if (!EmbedTexts_OnGameThread(ModelId, One, BatchOut, OutResponse.Error, true))
		{
			return false;
		}
		if (BatchOut.Num() != 1)
		{
			OutResponse.Error = TEXT("Embeddings response count mismatch.");
			return false;
		}
		OutResponse.Vector = MoveTemp(BatchOut[0]);
		return OutResponse.Vector.Num() > 0;
	}

	if (!IsInGameThread())
	{
		const TSharedPtr<FEmbedGameThreadSync> Sync = MakeShared<FEmbedGameThreadSync>();
		if (!Sync->DoneEvent)
		{
			OutResponse.Error = TEXT("Failed to allocate sync event for game-thread embedding dispatch.");
			return false;
		}

		const FString ModelIdCopy(ModelId);
		const FString InputCopy(Request.InputText);
		const TSharedPtr<TArray<TArray<float>>> GameThreadVectors = MakeShared<TArray<TArray<float>>>();
		const TSharedPtr<FString> GameThreadError = MakeShared<FString>();
		struct FSyncOutcome
		{
			bool bOk = false;
		};
		const TSharedPtr<FSyncOutcome> Outcome = MakeShared<FSyncOutcome>();

		AsyncTask(ENamedThreads::GameThread, [this, ModelIdCopy, InputCopy, GameThreadVectors, GameThreadError, Outcome, Sync]()
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

			TArray<FString> Single;
			Single.Add(InputCopy);
			Outcome->bOk = EmbedTexts_OnGameThread(ModelIdCopy, Single, *GameThreadVectors, *GameThreadError, false);
		});

		const bool bSignaled = Sync->DoneEvent->Wait(static_cast<uint32>(
			FMath::Max(1000.0f, (UnrealAiWaitTime::EmbeddingHttpTimeoutSec * 1000.0f) + 2000.0f)));
		if (!bSignaled)
		{
			OutResponse.Error = TEXT("Timed out waiting for game-thread embedding dispatch.");
			return false;
		}
		if (!Outcome->bOk || GameThreadVectors->Num() != 1)
		{
			OutResponse.Error = GameThreadError->IsEmpty() ? TEXT("Embedding failed.") : *GameThreadError;
			return false;
		}
		OutResponse.Vector = MoveTemp((*GameThreadVectors)[0]);
		return OutResponse.Vector.Num() > 0;
	}

	TArray<TArray<float>> BatchOut;
	if (!EmbedTexts_OnGameThread(ModelId, One, BatchOut, OutResponse.Error, false))
	{
		return false;
	}
	if (BatchOut.Num() != 1)
	{
		OutResponse.Error = TEXT("Embeddings response count mismatch.");
		return false;
	}
	OutResponse.Vector = MoveTemp(BatchOut[0]);
	return OutResponse.Vector.Num() > 0;
}

bool FOpenAiCompatibleEmbeddingProvider::EmbedBatch(
	const FString& ModelId,
	const TArray<FString>& InputTexts,
	bool bBackgroundIndexer,
	TArray<TArray<float>>& OutVectors,
	FString& OutError)
{
	if (InputTexts.Num() == 0)
	{
		OutError = TEXT("EmbedBatch: empty input list.");
		return false;
	}

	if (bBackgroundIndexer)
	{
		return EmbedTexts_OnGameThread(ModelId, InputTexts, OutVectors, OutError, true);
	}

	if (!IsInGameThread())
	{
		const TSharedPtr<FEmbedGameThreadSync> Sync = MakeShared<FEmbedGameThreadSync>();
		if (!Sync->DoneEvent)
		{
			OutError = TEXT("Failed to allocate sync event for game-thread embedding batch dispatch.");
			return false;
		}

		const FString ModelIdCopy(ModelId);
		const TArray<FString> InputsCopy(InputTexts);
		const TSharedPtr<TArray<TArray<float>>> OutPtr = MakeShared<TArray<TArray<float>>>();
		const TSharedPtr<FString> ErrPtr = MakeShared<FString>();
		struct FSyncOutcome
		{
			bool bOk = false;
		};
		const TSharedPtr<FSyncOutcome> Outcome = MakeShared<FSyncOutcome>();

		AsyncTask(ENamedThreads::GameThread, [this, ModelIdCopy, InputsCopy, OutPtr, ErrPtr, Outcome, Sync]()
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

			Outcome->bOk = EmbedTexts_OnGameThread(ModelIdCopy, InputsCopy, *OutPtr, *ErrPtr, false);
		});

		const uint32 WaitMs = static_cast<uint32>(FMath::Max(
			1000.0f,
			(UnrealAiWaitTime::EmbeddingHttpTimeoutSec * 1000.0f) + 2000.0f));
		const bool bSignaled = Sync->DoneEvent->Wait(WaitMs);
		if (!bSignaled)
		{
			OutError = TEXT("Timed out waiting for game-thread embedding batch dispatch.");
			return false;
		}
		if (!Outcome->bOk)
		{
			OutError = ErrPtr->IsEmpty() ? TEXT("Embedding batch failed.") : *ErrPtr;
			return false;
		}
		OutVectors = MoveTemp(*OutPtr);
		return OutVectors.Num() == InputTexts.Num();
	}

	return EmbedTexts_OnGameThread(ModelId, InputTexts, OutVectors, OutError, false);
}

bool FOpenAiCompatibleEmbeddingProvider::EmbedTexts_OnGameThread(
	const FString& ModelId,
	const TArray<FString>& InputTexts,
	TArray<TArray<float>>& OutVectors,
	FString& OutError,
	const bool bCallerThreadPumpsHttp)
{
	OutVectors.Reset();

	if (!Profiles)
	{
		OutError = TEXT("Embedding provider missing model profile registry.");
		return false;
	}

	FString BaseUrl;
	FString ApiKey;
	if (!Profiles->TryResolveApiForModel(ModelId, BaseUrl, ApiKey) || ApiKey.IsEmpty())
	{
		OutError = TEXT("No API key configured for embeddings.");
		return false;
	}
	while (!BaseUrl.IsEmpty() && BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1, EAllowShrinking::No);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), ModelId);
	TArray<TSharedPtr<FJsonValue>> Inputs;
	Inputs.Reserve(InputTexts.Num());
	for (const FString& T : InputTexts)
	{
		Inputs.Add(MakeShared<FJsonValueString>(T));
	}
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

	const float BaseTimeout = UnrealAiWaitTime::EmbeddingHttpTimeoutSec;
	const float TimeoutScale = FMath::Clamp(FMath::Sqrt(static_cast<float>(FMath::Max(1, InputTexts.Num()))), 1.f, 3.f);
	const float TimeoutSec = BaseTimeout * TimeoutScale;
	HttpRequest->SetTimeout(TimeoutSec);

	UnrealAiHarnessTpmThrottle::MaybeWaitBeforeEmbeddingBatchRequest(InputTexts);

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
		OutError = TEXT("Failed to allocate sync event for embeddings request.");
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
		OutError = TEXT("Failed to start embeddings request.");
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
		return false;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	const uint32 WaitMs = static_cast<uint32>(FMath::Max(1000.0f, (TimeoutSec * 1000.0f) + 100.0f));
	const bool bSignaled = bCallerThreadPumpsHttp ? WaitEmbeddingPollHttpOnCallerThread(DoneEvent, WaitMs)
												   : WaitForEventWhilePumpingHttpAndGameThread(DoneEvent, WaitMs);
	const double ElapsedMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	UE_LOG(
		LogTemp,
		Display,
		TEXT("Retrieval embeddings HTTP model=%s timeout=%.2fs waited_ms=%.0f batch=%d input_chars=%d url=%s"),
		*ModelId,
		TimeoutSec,
		ElapsedMs,
		InputTexts.Num(),
		TotalInputChars(InputTexts),
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
		OutError = FString::Printf(
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
		OutError = FString::Printf(
			TEXT("Embeddings HTTP request failed (status=%d, body=%s)"),
			StatusCode,
			*BodyPreview);
		return false;
	}

	if (!UnrealAiOpenAiBatchEmbeddingsParse::ParseEmbeddingsDataFromResponse(ResponseBody, InputTexts.Num(), OutVectors, OutError))
	{
		return false;
	}
	return true;
}
