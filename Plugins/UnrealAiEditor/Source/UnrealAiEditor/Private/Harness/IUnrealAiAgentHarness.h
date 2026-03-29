#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

class IAgentRunSink;
class FUnrealAiPlanExecutor;

/** Single entry for UI: one user turn (may include multiple LLM round-trips for tools). */
class IUnrealAiAgentHarness
{
public:
	virtual ~IUnrealAiAgentHarness() = default;

	virtual void RunTurn(const FUnrealAiAgentTurnRequest& Request, TSharedPtr<IAgentRunSink> Sink) = 0;

	virtual void CancelTurn() = 0;

	/** Headed harness: best-effort Fail() on the active turn before CancelTurn when idle-abort triggers (explicit jsonl error). */
	virtual void FailInProgressTurnForScenarioIdleAbort() {}

	/** True while a turn is actively streaming or executing tools. */
	virtual bool IsTurnInProgress() const = 0;

	/** True while the LLM transport has an in-flight HTTP request (idle abort must not fire). */
	virtual bool HasActiveLlmTransportRequest() const { return false; }

	/** True while streamed tools or tool queue work is in progress (idle abort must not fire). */
	virtual bool ShouldSuppressIdleAbort() const { return false; }

	/** Plan executor registered while a plan run is active (harness sync may wait across planner/node gaps). */
	virtual void NotifyPlanExecutorStarted(TSharedPtr<FUnrealAiPlanExecutor> Exec) { (void)Exec; }
	virtual void NotifyPlanExecutorEnded() {}
	virtual bool IsPlanPipelineActive() const { return false; }

	/**
	 * Headed `UnrealAiHarnessScenarioRunner` only: enable stricter per-turn tool caps (e.g. snapshot read spam).
	 * No-op for default; UI/chat leaves this off.
	 */
	virtual void SetHeadedScenarioStrictToolBudgets(bool bEnable) { (void)bEnable; }
};
