#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

class IUnrealAiAgentHarness;
class IAgentRunSink;

/** Level-B: sequential worker runs with deterministic merge (v1). Parallelism reserved for later. */
class FUnrealAiWorkerOrchestrator
{
public:
	/** Merge-only: union summaries and artifacts for parent display / logging. */
	static FUnrealAiWorkerResult MergeDeterministic(const TArray<FUnrealAiWorkerResult>& Workers);

	/**
	 * Run multiple worker goals sequentially (same project, derived thread ids).
	 * ParentSink receives OnRunStarted with WorkerIndex in Ids; each worker completion updates merge buffer.
	 */
	static void RunSequentialWorkers(
		IUnrealAiAgentHarness& Harness,
		const TArray<FString>& WorkerGoals,
		const FUnrealAiAgentTurnRequest& Template,
		TSharedPtr<IAgentRunSink> ParentSink,
		int32 MaxParallelismCap = 4);
};
