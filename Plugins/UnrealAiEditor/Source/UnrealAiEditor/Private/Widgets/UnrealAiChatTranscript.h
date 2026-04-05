#pragma once

#include "CoreMinimal.h"

#include "Harness/UnrealAiAgentTypes.h"

class FUnrealAiToolCatalog;
#include "Tools/Presentation/UnrealAiToolEditorPresentation.h"

enum class EUnrealAiChatBlockKind : uint8
{
	User,
	Thinking,
	Assistant,
	ToolCall,
	TodoPlan,
	/** Plan-mode DAG awaiting Build (editable JSON persisted separately). */
	PlanDraftPending,
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
	/** When Kind==User and not harness-injected: mode used for that send (for badge UI). */
	bool bHasUserAgentMode = false;
	EUnrealAiAgentMode UserAgentMode = EUnrealAiAgentMode::Agent;
	FString ThinkingText;
	FString AssistantText;
	/** Canonical tool id (matches catalog / harness). */
	FString ToolName;
	/** User-visible title; empty means derive in UI via UnrealAiResolveToolUserFacingName. */
	FString ToolDisplayTitle;
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
	FString ProgressDetails;
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

/**
 * Remove `<chat-name: ...>` tokens from InOutText (supports repeats).
 * Returns true if any token was removed. OutChatName receives the last non-empty parsed short name.
 */
bool UnrealAiStripChatNameTagsFromText(FString& InOutText, FString& OutChatName);

/** True when TrimmedLine is an echoed transcript/export header like "--- User ---", not a bare markdown "---". */
bool UnrealAiIsTranscriptStyleDelimiterTrimmedLine(const FString& TrimmedLine);

/**
 * Remove echoed transcript section headers (e.g. "--- User ---", "--- Tool: foo ---"), standalone "---"
 * horizontal-rule lines, and full-line "[Harness] ..." logs from model-visible chat text.
 */
void UnrealAiStripTranscriptStyleDelimiterLines(FString& InOutText);

/** True for standalone "---" lines, "--- Section ---" transcript headers, and full-line "[Harness] ..." logs. */
bool UnrealAiIsTranscriptNoiseOrHarnessDisplayLine(const FString& TrimmedLine);

/** Dock/tab title from a user bubble; empty for harness-injected user rows or whitespace-only. Strips chat-name tags. */
FString UnrealAiMakeChatTabTitleFromUserMessage(const FString& UserText);

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
	/** @param SentMode If non-null, show a mode badge on the user bubble (ignored for harness user lines). */
	FGuid AddUserMessage(const FString& Text, FGuid DesiredId = FGuid(), const EUnrealAiAgentMode* SentMode = nullptr);
	void BeginRun(const FGuid& RunId);
	void AppendThinkingDelta(const FString& Chunk);
	void AppendAssistantDelta(const FString& Chunk);
	void BeginToolCall(
		const FString& ToolName,
		const FString& CallId,
		const FString& ArgsPreview,
		const FString& ToolDisplayTitle = FString());
	void EndToolCall(
		const FString& CallId,
		bool bSuccess,
		const FString& ResultPreview = FString(),
		const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation = nullptr);
	void AddTodoPlan(const FString& Title, const FString& PlanJson);
	/** @return Block id for the draft row. */
	FGuid AddPlanDraftPending(const FString& DagJson);
	void RemovePlanDraftPendingBlocks();
	void SetPlanDraftJsonForBlock(const FGuid& BlockId, const FString& DagJson);
	void SetRunProgress(const FString& Label);
	void AppendRunEvent(const FString& EventLine);
	void ClearRunProgress();
	void EndRun(bool bSuccess, const FString& ErrorMessage);
	void OnContinuation(int32 PhaseIndex, int32 TotalPhasesHint);
	/** Non-error notice (e.g. context policy dropped an attachment). */
	void AddInformationalNotice(const FString& Text);
	/** Blocking editor modal appeared while a tool was running (overwrite, confirm, etc.). */
	void AddEditorBlockingDialogNotice(const FString& Summary);

	/** Rebuild UI blocks from persisted harness conversation messages (conversation.json). */
	void HydrateFromConversationMessages(
		const TArray<FUnrealAiConversationMessage>& Messages,
		const FUnrealAiToolCatalog* CatalogOpt = nullptr);

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
