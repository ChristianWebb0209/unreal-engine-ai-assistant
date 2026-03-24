#pragma once

#include "CoreMinimal.h"

#include "Harness/UnrealAiAgentTypes.h"
#include "Tools/Presentation/UnrealAiToolEditorPresentation.h"

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
	/** Synthetic harness nudges stored as user role in conversation.json ([Harness] prefix). */
	bool bHarnessSystemUser = false;
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

	/** FPlatformTime::Seconds() when this step started; 0 if not tracking. */
	double StepMonotonicStart = 0.0;
	/** e.g. "Thinking · 2m 34s" — shown above agent-side blocks when the step has finished. */
	FString StepTimingCaption;
};

DECLARE_MULTICAST_DELEGATE(FUnrealAiChatStructuralDelegate);
DECLARE_MULTICAST_DELEGATE_OneParam(FUnrealAiChatAssistantDeltaDelegate, const FString& /*Chunk*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FUnrealAiChatThinkingDeltaDelegate, const FString& /*Chunk*/, bool /*bFirstChunk*/);

/** Human-readable duration for step timers (e.g. "45s", "2m 3s"). */
FString UnrealAiFormatStepDurationForUi(double Seconds);

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
	/** Blocking editor modal appeared while a tool was running (overwrite, confirm, etc.). */
	void AddEditorBlockingDialogNotice(const FString& Summary);

	/** Rebuild UI blocks from persisted harness conversation messages (conversation.json). */
	void HydrateFromConversationMessages(const TArray<FUnrealAiConversationMessage>& Messages);

	/** True while the current run still has an open assistant reply segment (streaming or single-chunk). */
	bool IsAssistantSegmentOpen() const { return bAssistantSegmentOpen; }

	/**
	 * True when assistant text chunks arrived within the last QuietSeconds (network still streaming).
	 * Used to hide a live elapsed footer while tokens are actively arriving.
	 */
	bool IsAssistantStreamRecentlyActive(float QuietSeconds = 0.28f) const;

private:
	FGuid ActiveRunId;
	bool bHasActiveRun = false;
	bool bAssistantSegmentOpen = false;
	bool bThinkingOpen = false;
	double LastAssistantStreamChunkMonotonicTime = 0.0;

	int32 FindToolIndexByCallId(const FString& CallId) const;
	void CloseAssistantSegment();
	void TouchAssistantStreamChunkTime();
};
