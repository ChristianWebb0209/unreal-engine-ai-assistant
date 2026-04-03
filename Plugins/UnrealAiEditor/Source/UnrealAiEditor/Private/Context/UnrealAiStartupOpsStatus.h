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

	/** Three segments for a single-line status strip (startup aggregate, discovery, retrieval). */
	struct FFooterStrip
	{
		FString Aggregate;
		FString Discovery;
		FString Retrieval;
	};
	void BuildFooterStrip(const TSharedPtr<FUnrealAiBackendRegistry>& BackendRegistry, const FString& ProjectId, FFooterStrip& Out);
}
