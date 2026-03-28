#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

class IUnrealAiAgentHarness;
class IAgentRunSink;

/** Deterministic merge of structured worker summaries (used by `worker_merge_results` tool). */
class FUnrealAiWorkerOrchestrator
{
public:
	static FUnrealAiWorkerResult MergeDeterministic(const TArray<FUnrealAiWorkerResult>& Workers);
};
