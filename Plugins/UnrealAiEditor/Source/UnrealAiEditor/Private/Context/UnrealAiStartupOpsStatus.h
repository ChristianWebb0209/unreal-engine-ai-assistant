#pragma once

#include "CoreMinimal.h"

class FUnrealAiBackendRegistry;
class IUnrealAiRetrievalService;
struct FProjectTreeSummary;

namespace UnrealAiStartupOpsStatus
{
	struct FStatus
	{
		FString AggregateState;
		FString DiscoveryState;
		FString RetrievalState;
	};

	FStatus BuildStatus(IUnrealAiRetrievalService* RetrievalService, const FProjectTreeSummary& Summary, const FString& ProjectId);
	FString BuildCompactLine(const FStatus& Status);
	FString BuildCompactLine(const TSharedPtr<FUnrealAiBackendRegistry>& BackendRegistry, const FString& ProjectId);
}
