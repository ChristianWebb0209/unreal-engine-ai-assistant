#pragma once

#include "CoreMinimal.h"
#include "Harness/IAgentRunSink.h"

class FUnrealAiChatTranscript;

/** Bridges harness streaming events to FUnrealAiChatTranscript (chat UI). */
class FUnrealAiChatRunSink final : public IAgentRunSink
{
public:
	explicit FUnrealAiChatRunSink(TSharedPtr<FUnrealAiChatTranscript> InTranscript);

	virtual void OnRunStarted(const FUnrealAiRunIds& Ids) override;
	virtual void OnAssistantDelta(const FString& Chunk) override;
	virtual void OnThinkingDelta(const FString& Chunk) override;
	virtual void OnToolCallStarted(const FString& ToolName, const FString& CallId, const FString& ArgumentsJson) override;
	virtual void OnToolCallFinished(
		const FString& ToolName,
		const FString& CallId,
		bool bSuccess,
		const FString& ResultPreview) override;
	virtual void OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint) override;
	virtual void OnTodoPlanEmitted(const FString& Title, const FString& PlanJson) override;
	virtual void OnRunFinished(bool bSuccess, const FString& ErrorMessage) override;

private:
	TSharedPtr<FUnrealAiChatTranscript> Transcript;
};
