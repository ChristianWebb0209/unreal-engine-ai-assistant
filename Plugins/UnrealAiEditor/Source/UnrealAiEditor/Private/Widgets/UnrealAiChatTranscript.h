#pragma once

#include "CoreMinimal.h"

struct FUnrealAiToolEditorPresentation;

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
	TSharedPtr<FUnrealAiToolEditorPresentation> ToolEditorPresentation;
	bool bToolRunning = false;
	bool bToolOk = false;
	FString NoticeText;
	bool bNoticeError = false;
	FString TodoTitle;
	FString TodoJson;
	FString ProgressLabel;
	bool bRunCancelled = false;
};

/** Tool calls that follow an assistant segment (for verbose UI). */
struct FUnrealAiAssistantSegmentToolInfo
{
	FString ToolName;
	FString ToolCallId;
	FString ArgsPreview;
	FString ResultPreview;
	bool bRunning = false;
	bool bOk = false;
};

/** Ordered tool names following this assistant block until next Assistant or User (inclusive of intermediate non-tool blocks). */
void UnrealAiCollectToolsAfterAssistant(
	const TArray<FUnrealAiChatBlock>& Blocks,
	int32 AssistantIndex,
	TArray<FString>& OutOrderedToolNames);

/** Full tool-call rows after an assistant block (same span as UnrealAiCollectToolsAfterAssistant). */
void UnrealAiCollectToolDetailsAfterAssistant(
	const TArray<FUnrealAiChatBlock>& Blocks,
	int32 AssistantIndex,
	TArray<FUnrealAiAssistantSegmentToolInfo>& OutDetails);

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
	/** Plain-text export / clipboard (game-thread snapshot of Blocks). */
	FString FormatPlainText() const;
	/** @return Id of the new user block (for attaching async metadata). Pass a valid DesiredId to control the block id (e.g. UI animation coordination). */
	FGuid AddUserMessage(const FString& Text, FGuid DesiredId = FGuid());
	void BeginRun(const FGuid& RunId);
	void AppendThinkingDelta(const FString& Chunk);
	void AppendAssistantDelta(const FString& Chunk);
	void BeginToolCall(const FString& ToolName, const FString& CallId, const FString& ArgsPreview);
	void EndToolCall(
		const FString& CallId,
		bool bSuccess,
		const FString& ResultPreview = FString(),
		const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation = nullptr);
	void AddTodoPlan(const FString& Title, const FString& PlanJson);
	void SetRunProgress(const FString& Label);
	void EndRun(bool bSuccess, const FString& ErrorMessage);
	void OnContinuation(int32 PhaseIndex, int32 TotalPhasesHint);
	/** Non-error notice (e.g. context policy dropped an attachment). */
	void AddInformationalNotice(const FString& Text);

	/** True while the current run still has an open assistant reply segment (streaming or single-chunk). */
	bool IsAssistantSegmentOpen() const { return bAssistantSegmentOpen; }

private:
	FGuid ActiveRunId;
	bool bHasActiveRun = false;
	bool bAssistantSegmentOpen = false;
	bool bThinkingOpen = false;

	int32 FindToolIndexByCallId(const FString& CallId) const;
	void CloseAssistantSegment();
};
