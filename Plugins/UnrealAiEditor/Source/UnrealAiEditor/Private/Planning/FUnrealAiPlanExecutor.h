#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Planning/UnrealAiPlanDag.h"

class IUnrealAiAgentHarness;
class IAgentContextService;
class IAgentRunSink;

struct FUnrealAiPlanExecutorStartOptions
{
	/** When true (editor Plan chat), stop after planner parse until ResumeNodeExecution / ResumeExecutionFromDag. */
	bool bPauseAfterPlannerForBuild = false;
	/** Headed harness / smoke: after valid DAG parse, finish successfully without running agent node turns. */
	bool bHarnessPlannerOnlyNoExecute = false;
	/** When true, ignore editor `bPlanAutoReplan` for this run. */
	bool bDisableAutoReplan = false;
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

	/**
	 * Headed scenario only: if the plan pipeline hits the scenario wall while **idle** between sub-turns, run one
	 * compact Plan-mode replan and merge the DAG; extends the wall deadline from now. Returns false if skipped
	 * (disabled, budget exhausted, turn in progress, or parse/merge failed).
	 */
	bool TryScenarioWallCompactReplanForHeadedHarness(
		const TSharedPtr<IAgentRunSink>& HarnessSink,
		double& InOutWallDeadlineSec,
		uint32 ScenarioWallMs);

private:
	void BeginPlannerTurn();
	void OnPlannerFinished(bool bSuccess, const FString& ErrorText, const FString& PlannerText);
	void BeginNextReadyNode();
	void OnNodeFinished(const FString& NodeId, bool bSuccess, const FString& ErrorText, const FString& AssistantText);
	void Finish(bool bSuccess, const FString& ErrorText);

	void BeginNodeFailureReplanTurn(const FString& FailedNodeId, const FString& ErrorText);
	void OnNodeFailureReplanPlannerFinished(bool bSuccess, const FString& ErrorText, const FString& PlannerText);
	bool TryApplyReplanPlannerAssistantText(const FString& PlannerText, FString& OutError);
	void RecomputeAnyPlanNodeFailedFromContext();
	static void PumpGameThreadForHarnessWait(uint32 MaxWaitMs);

	/** After a node fails, mark transitive dependents skipped so the DAG cannot deadlock on failed deps. */
	void CascadeSkipDependentsAfterFailure(const FString& FailedNodeId, const FString& ErrorText);
	/** When no ready nodes remain, derive success from context (any failed node => Finish false). */
	void FinishWhenDagFullyResolved();

	/** If UnrealAiWaitTime::HarnessPlanMaxWallMs > 0, wall clock from PlanWallStartSec must stay within budget between planner/node segments. */
	bool CheckPlanWallBudgetOrFinish();

	bool ComputePlanOverallSuccess();
	FString BuildPlanFailureRollupMessage();

	IUnrealAiAgentHarness* Harness = nullptr;
	IAgentContextService* ContextService = nullptr;
	FUnrealAiAgentTurnRequest ParentRequest;
	TSharedPtr<IAgentRunSink> ParentSink;
	bool bRunning = false;
	bool bCancelled = false;
	bool bAwaitingBuild = false;
	bool bPauseAfterPlannerForBuild = false;
	bool bHarnessPlannerOnlyNoExecute = false;
	/** 0 = planner phase complete; 1..N = node execution phase index (for sink continuation). */
	int32 PlanExecutionPhase = 0;
	TArray<FString> ReadyNodeIds;
	FUnrealAiPlanDag Dag;
	FUnrealAiRunIds ParentIds;

	/** 0 = disabled. Cached when a plan run starts (see UnrealAiWaitTime::HarnessPlanMaxWallMs). */
	uint32 PlanMaxWallMsCached = 0;
	/** Monotonic seconds when the active plan execution window started (planner + nodes; resets after pause-for-build resume). */
	double PlanWallStartSec = 0.0;

	/** Snapshot of user message for the plan run (repair prompts append harness validation errors). */
	FString OriginalPlannerUserText;
	/** When non-empty, the next planner `RunTurn` uses this as `UserText` instead of `ParentRequest.UserText`. */
	FString PendingPlannerUserTextOverride;
	/** After one failed parse/validate, a second planner pass may run with augmented user text; only one repair. */
	bool bPlannerDagRepairConsumed = false;

	/** True if any plan node harness turn ended with bSuccess==false (covers missing context service). */
	bool bAnyPlanNodeFailedThisRun = false;

	bool bPlanAutoReplan = false;
	int32 PlanAutoReplanMaxAttempts = 0;
	int32 PlanAutoReplanAttemptsUsed = 0;
	bool bUseSubagentsPolicy = false;
	int32 MaxParallelWaveNodesPolicy = 1;
	bool bScenarioWallReplanConsumed = false;
	bool bNodeFailureReplanRepairConsumed = false;
	FString PendingNodeFailureReplanBaseUserText;
	FString PendingNodeFailureReplanFailedNodeId;
	FString PendingNodeFailureReplanErrorText;
};
