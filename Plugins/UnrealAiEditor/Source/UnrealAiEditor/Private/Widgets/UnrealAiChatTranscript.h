#pragma once

#include "CoreMinimal.h"

enum class EUnrealAiChatBlockKind : uint8
{
	User,
	Thinking,
	Assistant,
	ToolCall,
	TodoPlan,
	Notice,
	RunProgress,
};

/** One row in the chat transcript (game-thread model). */
struct FUnrealAiChatBlock
{
	FGuid Id;
	FGuid RunId;
	int32 PhaseIndex = 0;
	EUnrealAiChatBlockKind Kind = EUnrealAiChatBlockKind::Assistant;

	FString UserText;
	FString ThinkingText;
	FString AssistantText;
	FString ToolName;
	FString ToolCallId;
	FString ToolArgsPreview;
	FString ToolResultPreview;
	bool bToolRunning = false;
	bool bToolOk = false;
	FString NoticeText;
	bool bNoticeError = false;
	FString TodoTitle;
	FString TodoJson;
	FString ProgressLabel;
	bool bRunCancelled = false;
};

/** Ordered tool names following this assistant block until next Assistant or User (inclusive of intermediate non-tool blocks). */
void UnrealAiCollectToolsAfterAssistant(
	const TArray<FUnrealAiChatBlock>& Blocks,
	int32 AssistantIndex,
	TArray<FString>& OutOrderedToolNames);

DECLARE_MULTICAST_DELEGATE(FUnrealAiChatStructuralDelegate);
DECLARE_MULTICAST_DELEGATE_OneParam(FUnrealAiChatAssistantDeltaDelegate, const FString& /*Chunk*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FUnrealAiChatThinkingDeltaDelegate, const FString& /*Chunk*/, bool /*bFirstChunk*/);

/** Mutable transcript for the chat UI; updated by FUnrealAiChatRunSink. */
class FUnrealAiChatTranscript : public TSharedFromThis<FUnrealAiChatTranscript>
{
public:
	TArray<FUnrealAiChatBlock> Blocks;

	FUnrealAiChatStructuralDelegate OnStructuralChange;
	FUnrealAiChatAssistantDeltaDelegate OnAssistantStreamDelta;
	FUnrealAiChatThinkingDeltaDelegate OnThinkingStreamDelta;

	void Clear();
	void AddUserMessage(const FString& Text);
	void BeginRun(const FGuid& RunId);
	void AppendThinkingDelta(const FString& Chunk);
	void AppendAssistantDelta(const FString& Chunk);
	void BeginToolCall(const FString& ToolName, const FString& CallId, const FString& ArgsPreview);
	void EndToolCall(const FString& CallId, bool bSuccess, const FString& ResultPreview = FString());
	void AddTodoPlan(const FString& Title, const FString& PlanJson);
	void SetRunProgress(const FString& Label);
	void EndRun(bool bSuccess, const FString& ErrorMessage);
	void OnContinuation(int32 PhaseIndex, int32 TotalPhasesHint);

private:
	FGuid ActiveRunId;
	bool bHasActiveRun = false;
	bool bAssistantSegmentOpen = false;
	bool bThinkingOpen = false;

	int32 FindToolIndexByCallId(const FString& CallId) const;
	void CloseAssistantSegment();
};
