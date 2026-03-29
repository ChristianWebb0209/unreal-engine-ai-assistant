#pragma once

#include "CoreMinimal.h"
#include "Harness/IAgentRunSink.h"

#include <atomic>

class IAgentContextService;
class FEvent;

/**
 * Writes harness run events to a JSONL file (one JSON object per line) for automation / LLM-in-the-loop review.
 * Optionally dumps BuildContextWindow text after each tool or on completion.
 */
class FAgentRunFileSink final : public IAgentRunSink
{
public:
	/**
	 * @param JsonlPath Absolute path for append output (parent dir should exist).
	 * @param ContextService If non-null, used for optional context dumps.
	 * @param bDumpContextAfterEachTool If true, writes context_window snapshot after each tool finishes.
	 * @param bDumpContextOnRunFinished If true, writes final context_window at end.
	 * @param DoneEvent If non-null, Trigger() is called from OnRunFinished (success or failure).
	 * @param OutSuccess If non-null, written with harness success before DoneEvent triggers.
	 * @param OutFinishError If non-null, written with error text on failure.
	 * @param PlanSubTurnEvent If non-null (manual-reset), Trigger() from OnPlanHarnessSubTurnComplete for plan-mode budget resets.
	 */
	FAgentRunFileSink(
		FString JsonlPath,
		IAgentContextService* ContextService,
		const FString& ProjectId,
		const FString& ThreadId,
		bool bDumpContextAfterEachTool,
		bool bDumpContextOnRunFinished,
		FEvent* DoneEvent,
		bool* OutSuccess = nullptr,
		FString* OutFinishError = nullptr,
		FEvent* PlanSubTurnEvent = nullptr);

	virtual ~FAgentRunFileSink() override;

	virtual void OnRunStarted(const FUnrealAiRunIds& Ids) override;
	virtual void OnContextUserMessages(const TArray<FString>& Messages) override;
	virtual void OnAssistantDelta(const FString& Chunk) override;
	virtual void OnThinkingDelta(const FString& Chunk) override;
	virtual void OnToolCallStarted(const FString& ToolName, const FString& CallId, const FString& ArgumentsJson) override;
	virtual void OnToolCallFinished(
		const FString& ToolName,
		const FString& CallId,
		bool bSuccess,
		const FString& ResultPreview,
		const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation) override;
	virtual void OnEditorBlockingDialogDuringTools(const FString& Summary) override;
	virtual void OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint) override;
	virtual void OnTodoPlanEmitted(const FString& Title, const FString& PlanJson) override;
	virtual void OnPlanningDecision(const FString& ModeUsed, const TArray<FString>& TriggerReasons, int32 ReplanCount, int32 QueueStepsPending) override;
	virtual void OnEnforcementEvent(const FString& EventType, const FString& Detail) override;
	virtual void OnEnforcementSummary(
		int32 ActionIntentTurns,
		int32 ActionTurnsWithToolCalls,
		int32 ActionTurnsWithExplicitBlocker,
		int32 ActionNoToolNudges,
		int32 MutationIntentTurns,
		int32 MutationReadOnlyNudges) override;
	virtual void OnRunFinished(bool bSuccess, const FString& ErrorMessage) override;
	virtual void OnPlanHarnessSubTurnComplete() override;
	virtual void OnLlmRequestPreparedForHttp(
		const FUnrealAiAgentTurnRequest& TurnRequest,
		const FGuid& RunId,
		int32 LlmRound,
		int32 EffectiveMaxLlmRounds,
		const FUnrealAiLlmRequest& LlmRequest) override;

	const FString& GetJsonlPath() const { return JsonlPath; }

	/** Extra JSONL lines from the harness runner (e.g. timeout diagnostics) not routed through IAgentRunSink events. */
	void AppendHarnessDiagnosticJson(const TSharedPtr<FJsonObject>& Obj);

private:
	bool AppendJsonObject(const TSharedPtr<FJsonObject>& Obj, bool bLogOnFailure = true);
	bool AppendRunFinishedLineWithRetry(const TSharedPtr<FJsonObject>& Obj);
	void MaybeDumpContextWindow(const TCHAR* Reason);
	/** Plan-mode headed harness: signal automation to reset per-segment sync wait (PlanSubTurnEvent). */
	void NotifyPlanHarnessSyncSegmentBoundary();

	FString JsonlPath;
	IAgentContextService* ContextService = nullptr;
	FString ProjectId;
	FString ThreadId;
	bool bDumpContextAfterEachTool = false;
	bool bDumpContextOnRunFinished = true;
	FEvent* DoneEvent = nullptr;
	FEvent* PlanSubTurnEvent = nullptr;
	bool* CompletionSuccessPtr = nullptr;
	FString* CompletionErrorPtr = nullptr;
	std::atomic<bool> bFinished{false};
	/** Increments for each FUnrealAiPlanExecutor plan sub-turn (planner done, each node done). */
	int32 PlanSubTurnCompleteCount = 0;
};
