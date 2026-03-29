#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Harness/UnrealAiPlanDag.h"

class IUnrealAiAgentHarness;
class IAgentContextService;
class IAgentRunSink;

struct FUnrealAiPlanExecutorStartOptions
{
	/** When true (editor Plan chat), stop after planner parse until ResumeNodeExecution / ResumeExecutionFromDag. */
	bool bPauseAfterPlannerForBuild = false;
};

class FUnrealAiPlanExecutor : public TSharedFromThis<FUnrealAiPlanExecutor>
{
public:
	static TSharedRef<FUnrealAiPlanExecutor> Start(
		IUnrealAiAgentHarness* Harness,
		IAgentContextService* ContextService,
		const FUnrealAiAgentTurnRequest& ParentRequest,
		TSharedPtr<IAgentRunSink> ParentSink,
		const FUnrealAiPlanExecutorStartOptions& Options = FUnrealAiPlanExecutorStartOptions());

	/** Cold start: skip planner, run nodes from DAG JSON (e.g. after reload + Build). */
	static TSharedRef<FUnrealAiPlanExecutor> ResumeExecutionFromDag(
		IUnrealAiAgentHarness* Harness,
		IAgentContextService* ContextService,
		const FUnrealAiAgentTurnRequest& ParentRequest,
		TSharedPtr<IAgentRunSink> ParentSink,
		const FString& DagJsonText);

	void Cancel();
	bool IsRunning() const { return bRunning; }
	bool IsAwaitingBuild() const { return bRunning && bAwaitingBuild; }

	/** Apply possibly user-edited DAG text before resuming (must be valid). */
	bool ApplyDagJsonForBuild(const FString& DagJsonText, FString& OutError);

	/** Continue after user clicks Build (in-memory session). */
	void ResumeNodeExecution();

private:
	void BeginPlannerTurn();
	void OnPlannerFinished(bool bSuccess, const FString& ErrorText, const FString& PlannerText);
	void BeginNextReadyNode();
	void OnNodeFinished(const FString& NodeId, bool bSuccess, const FString& ErrorText, const FString& AssistantText);
	void Finish(bool bSuccess, const FString& ErrorText);

	/** If UnrealAiWaitTime::HarnessPlanMaxWallMs > 0, wall clock from PlanWallStartSec must stay within budget between planner/node segments. */
	bool CheckPlanWallBudgetOrFinish();

	IUnrealAiAgentHarness* Harness = nullptr;
	IAgentContextService* ContextService = nullptr;
	FUnrealAiAgentTurnRequest ParentRequest;
	TSharedPtr<IAgentRunSink> ParentSink;
	bool bRunning = false;
	bool bCancelled = false;
	bool bAwaitingBuild = false;
	bool bPauseAfterPlannerForBuild = false;
	/** 0 = planner phase complete; 1..N = node execution phase index (for sink continuation). */
	int32 PlanExecutionPhase = 0;
	TArray<FString> ReadyNodeIds;
	FUnrealAiPlanDag Dag;
	FUnrealAiRunIds ParentIds;

	/** 0 = disabled. Cached when a plan run starts (see UnrealAiWaitTime::HarnessPlanMaxWallMs). */
	uint32 PlanMaxWallMsCached = 0;
	/** Monotonic seconds when the active plan execution window started (planner + nodes; resets after pause-for-build resume). */
	double PlanWallStartSec = 0.0;
};
