#include "Harness/FUnrealAiLlmTransportFixture.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FUnrealAiLlmTransportFixture::FUnrealAiLlmTransportFixture(FString InFixturePath)
	: FixturePath(MoveTemp(InFixturePath))
{
}

bool FUnrealAiLlmTransportFixture::EnsureLoaded(FString& OutError)
{
	FScopeLock Lock(&LoadMutex);
	if (bLoaded)
	{
		return true;
	}
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *FixturePath))
	{
		OutError = FString::Printf(TEXT("Could not read fixture: %s"), *FixturePath);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Invalid JSON in fixture");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Responses = nullptr;
	if (!Root->TryGetArrayField(TEXT("responses"), Responses) || !Responses)
	{
		OutError = TEXT("Fixture missing responses[]");
		return false;
	}
	CachedResponses.Reset();
	for (const TSharedPtr<FJsonValue>& RV : *Responses)
	{
		const TSharedPtr<FJsonObject> RObj = RV.IsValid() ? RV->AsObject() : nullptr;
		if (!RObj.IsValid())
		{
			continue;
		}
		const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
		if (!RObj->TryGetArrayField(TEXT("events"), Events) || !Events)
		{
			continue;
		}
		TArray<FUnrealAiLlmStreamEvent> EventsOut;
		for (const TSharedPtr<FJsonValue>& EV : *Events)
		{
			const TSharedPtr<FJsonObject> EObj = EV.IsValid() ? EV->AsObject() : nullptr;
			if (!EObj.IsValid())
			{
				continue;
			}
			FString Type;
			EObj->TryGetStringField(TEXT("type"), Type);
			FUnrealAiLlmStreamEvent Ev;
			if (Type.Equals(TEXT("assistant_delta"), ESearchCase::IgnoreCase))
			{
				Ev.Type = EUnrealAiLlmStreamEventType::AssistantDelta;
				EObj->TryGetStringField(TEXT("text"), Ev.DeltaText);
			}
			else if (Type.Equals(TEXT("thinking_delta"), ESearchCase::IgnoreCase))
			{
				Ev.Type = EUnrealAiLlmStreamEventType::ThinkingDelta;
				EObj->TryGetStringField(TEXT("text"), Ev.DeltaText);
			}
			else if (Type.Equals(TEXT("tool_calls"), ESearchCase::IgnoreCase))
			{
				Ev.Type = EUnrealAiLlmStreamEventType::ToolCalls;
				const TArray<TSharedPtr<FJsonValue>>* Calls = nullptr;
				if (EObj->TryGetArrayField(TEXT("calls"), Calls) && Calls)
				{
					for (const TSharedPtr<FJsonValue>& CV : *Calls)
					{
						const TSharedPtr<FJsonObject> Co = CV.IsValid() ? CV->AsObject() : nullptr;
						if (!Co.IsValid())
						{
							continue;
						}
						FUnrealAiToolCallSpec Tc;
						Co->TryGetStringField(TEXT("id"), Tc.Id);
						Co->TryGetStringField(TEXT("name"), Tc.Name);
						Co->TryGetStringField(TEXT("arguments"), Tc.ArgumentsJson);
						Ev.ToolCalls.Add(Tc);
					}
				}
			}
			else if (Type.Equals(TEXT("finish"), ESearchCase::IgnoreCase))
			{
				Ev.Type = EUnrealAiLlmStreamEventType::Finish;
				EObj->TryGetStringField(TEXT("finish_reason"), Ev.FinishReason);
				if (Ev.FinishReason.IsEmpty())
				{
					Ev.FinishReason = TEXT("stop");
				}
			}
			else if (Type.Equals(TEXT("error"), ESearchCase::IgnoreCase))
			{
				Ev.Type = EUnrealAiLlmStreamEventType::Error;
				EObj->TryGetStringField(TEXT("message"), Ev.ErrorMessage);
			}
			else
			{
				continue;
			}
			EventsOut.Add(Ev);
		}
		if (EventsOut.Num() > 0)
		{
			CachedResponses.Add(MoveTemp(EventsOut));
		}
	}
	if (CachedResponses.Num() == 0)
	{
		OutError = TEXT("Fixture responses[] produced no event streams");
		return false;
	}
	bLoaded = true;
	return true;
}

void FUnrealAiLlmTransportFixture::StreamChatCompletion(
	const FUnrealAiLlmRequest& Request,
	const FUnrealAiLlmStreamCallback OnEvent)
{
	FString Err;
	if (!EnsureLoaded(Err))
	{
		FUnrealAiLlmStreamEvent E;
		E.Type = EUnrealAiLlmStreamEventType::Error;
		E.ErrorMessage = Err.IsEmpty() ? TEXT("Fixture load failed") : Err;
		OnEvent.ExecuteIfBound(E);
		return;
	}

	TArray<FUnrealAiLlmStreamEvent> Events;
	{
		FScopeLock Lock(&LoadMutex);
		if (NextResponseIndex >= CachedResponses.Num())
		{
			FUnrealAiLlmStreamEvent E;
			E.Type = EUnrealAiLlmStreamEventType::Error;
			E.ErrorMessage = FString::Printf(
				TEXT("Fixture exhausted (%d responses); add more to %s"),
				CachedResponses.Num(),
				*FixturePath);
			OnEvent.ExecuteIfBound(E);
			return;
		}
		Events = CachedResponses[NextResponseIndex++];
	}

	bCancelRequested.store(false, std::memory_order_relaxed);

	// Emit synchronously to avoid deadlocks in RunAgentTurnSync (which can block the game thread and prevent ticking).
	for (const FUnrealAiLlmStreamEvent& Ev : Events)
	{
		if (bCancelRequested.load(std::memory_order_relaxed))
		{
			FUnrealAiLlmStreamEvent E;
			E.Type = EUnrealAiLlmStreamEventType::Error;
			E.ErrorMessage = TEXT("Cancelled");
			OnEvent.ExecuteIfBound(E);
			return;
		}
		OnEvent.ExecuteIfBound(Ev);
	}
}

void FUnrealAiLlmTransportFixture::CancelActiveRequest()
{
	bCancelRequested.store(true, std::memory_order_relaxed);
}
