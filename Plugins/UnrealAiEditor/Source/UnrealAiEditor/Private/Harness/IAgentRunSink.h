#pragma once

#include "CoreMinimal.h"
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
	/** Multi-round continuation within one user message (plan execution, workers). */
	virtual void OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint) = 0;
	/** Structured todo plan from tool or harness (JSON body). */
	virtual void OnTodoPlanEmitted(const FString& Title, const FString& PlanJson) = 0;
	virtual void OnRunFinished(bool bSuccess, const FString& ErrorMessage) = 0;
};
