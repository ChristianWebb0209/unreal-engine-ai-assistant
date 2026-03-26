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
		FString* OutFinishError = nullptr);

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
	virtual void OnRunFinished(bool bSuccess, const FString& ErrorMessage) override;

	const FString& GetJsonlPath() const { return JsonlPath; }

private:
	void AppendJsonObject(const TSharedPtr<FJsonObject>& Obj);
	void MaybeDumpContextWindow(const TCHAR* Reason);

	FString JsonlPath;
	IAgentContextService* ContextService = nullptr;
	FString ProjectId;
	FString ThreadId;
	bool bDumpContextAfterEachTool = false;
	bool bDumpContextOnRunFinished = true;
	FEvent* DoneEvent = nullptr;
	bool* CompletionSuccessPtr = nullptr;
	FString* CompletionErrorPtr = nullptr;
	std::atomic<bool> bFinished{false};
};
