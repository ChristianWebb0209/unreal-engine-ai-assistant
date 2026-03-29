#pragma once

#include "CoreMinimal.h"
#include "Harness/ILlmTransport.h"
#include "Harness/UnrealAiAgentTypes.h"

struct FUnrealAiToolEditorPresentation;

DECLARE_DELEGATE_OneParam(FUnrealAiAssistantDeltaSink, const FString& /*Chunk*/);
DECLARE_DELEGATE(FUnrealAiTurnCompleteSink);

/** Observes harness lifecycle: streaming text, run ids, completion (UI + logs). */
class IAgentRunSink
{
public:
	virtual ~IAgentRunSink() = default;

	virtual void OnRunStarted(const FUnrealAiRunIds& Ids) = 0;
	/** Context layer dropped attachments (e.g. images) — show as informational chat lines before model output. */
	virtual void OnContextUserMessages(const TArray<FString>& Messages) {}
	virtual void OnAssistantDelta(const FString& Chunk) = 0;
	/** Provider reasoning / extended thinking stream (optional). */
	virtual void OnThinkingDelta(const FString& Chunk) = 0;
	virtual void OnToolCallStarted(const FString& ToolName, const FString& CallId, const FString& ArgumentsJson) = 0;
	/** ResultPreview is truncated tool output or error text for UI (may be empty). */
	virtual void OnToolCallFinished(
		const FString& ToolName,
		const FString& CallId,
		bool bSuccess,
		const FString& ResultPreview,
		const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation) = 0;
	/**
	 * Editor opened a blocking Slate modal while tools were running (e.g. overwrite / confirm).
	 * UI should surface this immediately; the harness also appends details to the tool message for the model.
	 */
	virtual void OnEditorBlockingDialogDuringTools(const FString& Summary) {}
	/** Multi-round continuation within one user message (plan execution, workers). */
	virtual void OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint) = 0;
	/** Structured todo plan from tool or harness (JSON body). */
	virtual void OnTodoPlanEmitted(const FString& Title, const FString& PlanJson) = 0;
	/** Plan-mode DAG JSON ready for user review / Build (editor UI only). */
	virtual void OnPlanDraftReady(const FString& DagJsonText) {}
	/**
	 * Plan harness only: planner turn or a plan-node agent turn has finished (next phase may start).
	 * Used by automation to reset per-segment sync wait budgets so a multi-node plan does not share one timeout.
	 */
	virtual void OnPlanHarnessSubTurnComplete() {}
	/** Planning-policy runtime signal for observability (agent mode heuristics/escalation). */
	virtual void OnPlanningDecision(const FString& ModeUsed, const TArray<FString>& TriggerReasons, int32 ReplanCount, int32 QueueStepsPending) {}
	/** Per-turn enforcement signal for action/mutation execution policy observability. */
	virtual void OnEnforcementEvent(const FString& EventType, const FString& Detail) {}
	/** Run-level aggregate counters for action/mutation policy auditing. */
	virtual void OnEnforcementSummary(
		int32 ActionIntentTurns,
		int32 ActionTurnsWithToolCalls,
		int32 ActionTurnsWithExplicitBlocker,
		int32 ActionNoToolNudges,
		int32 MutationIntentTurns,
		int32 MutationReadOnlyNudges) {}
	/**
	 * Immediately before the harness submits an HTTP chat-completions request (after UnrealAiTurnLlmRequestBuilder::Build).
	 * Default: no-op. FAgentRunFileSink logs every outbound request to llm_requests.jsonl unless disabled (editor Harness setting default on; UNREAL_AI_LOG_LLM_REQUESTS=0).
	 * API keys are not written; see payload field api_key_redacted.
	 */
	virtual void OnLlmRequestPreparedForHttp(
		const FUnrealAiAgentTurnRequest& TurnRequest,
		const FGuid& RunId,
		int32 LlmRound,
		int32 EffectiveMaxLlmRounds,
		const FUnrealAiLlmRequest& LlmRequest) {}
	virtual void OnRunFinished(bool bSuccess, const FString& ErrorMessage) = 0;

	/**
	 * Human-readable line for harness debugging (headed smoke tests). FAgentRunFileSink writes
	 * harness_progress.log next to run.jsonl so runs that hang still leave a trace on disk.
	 */
	virtual void OnHarnessProgressLog(const FString& Line) {}
};
