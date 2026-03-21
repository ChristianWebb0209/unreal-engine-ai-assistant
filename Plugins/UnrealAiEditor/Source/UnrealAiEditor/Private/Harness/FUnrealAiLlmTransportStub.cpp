#include "Harness/FUnrealAiLlmTransportStub.h"

#include "Containers/Ticker.h"

void FUnrealAiLlmTransportStub::StreamChatCompletion(const FUnrealAiLlmRequest& Request, FUnrealAiLlmStreamCallback OnEvent)
{
	FString LastUser;
	for (int32 i = Request.Messages.Num() - 1; i >= 0; --i)
	{
		if (Request.Messages[i].Role == TEXT("user"))
		{
			LastUser = Request.Messages[i].Content;
			break;
		}
	}
	const FString Full =
		FString::Printf(TEXT("[Offline stub — set api.baseUrl and api.apiKey in plugin settings] Last user message: %s"), *LastUser);

	int32 Idx = 0;
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[OnEvent, Full, Idx](float) mutable -> bool
			{
				if (Idx < Full.Len())
				{
					const int32 N = FMath::Min(8, Full.Len() - Idx);
					FUnrealAiLlmStreamEvent De;
					De.Type = EUnrealAiLlmStreamEventType::AssistantDelta;
					De.DeltaText = Full.Mid(Idx, N);
					Idx += N;
					OnEvent.ExecuteIfBound(De);
					return true;
				}
				FUnrealAiLlmStreamEvent Fin;
				Fin.Type = EUnrealAiLlmStreamEventType::Finish;
				Fin.FinishReason = TEXT("stop");
				OnEvent.ExecuteIfBound(Fin);
				return false;
			}),
		0.02f);
}
