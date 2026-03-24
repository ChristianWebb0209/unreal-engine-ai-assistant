#pragma once

#include "CoreMinimal.h"
#include "Harness/IAgentRunSink.h"

class FUnrealAiChatTranscript;
struct FUnrealAiChatUiSession;
class IUnrealAiPersistence;

/** Bridges harness streaming events to FUnrealAiChatTranscript (chat UI). */
class FUnrealAiChatRunSink final : public IAgentRunSink
{
public:
	explicit FUnrealAiChatRunSink(
		TSharedPtr<FUnrealAiChatTranscript> InTranscript,
		TSharedPtr<FUnrealAiChatUiSession> InSession,
		IUnrealAiPersistence* InPersistence,
		const FString& InProjectId,
		const FString& InThreadId);

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
	virtual void OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint) override;
	virtual void OnTodoPlanEmitted(const FString& Title, const FString& PlanJson) override;
	virtual void OnRunFinished(bool bSuccess, const FString& ErrorMessage) override;

private:
	TSharedPtr<FUnrealAiChatTranscript> Transcript;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	IUnrealAiPersistence* Persistence = nullptr;
	FString ProjectId;
	FString ThreadId;
};
