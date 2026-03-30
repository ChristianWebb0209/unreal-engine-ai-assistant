#pragma once

#include "CoreMinimal.h"
#include "Containers/Set.h"
#include "Layout/Visibility.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"

struct FUnrealAiConversationMessage;

class FUnrealAiChatTranscript;
class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;
class SAssistantStreamBlock;
class SThinkingSubline;

class SChatMessageList final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatMessageList) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiChatUiSession>, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	TSharedPtr<FUnrealAiChatTranscript> GetTranscript() const { return Transcript; }

	FGuid AddUserMessage(const FString& Text);
	void ClearTranscript();

	/** Replace transcript from disk (conversation.json) and rebuild the list. */
	void HydrateTranscriptFromPersistedConversation(const TArray<FUnrealAiConversationMessage>& Messages);

	/** @deprecated Prefer AddUserMessage; kept for call sites. */
	void ResetAssistant();

	/** Called when the user message entry animation finishes. */
	void ClearUserAnimId();

	/** While following the bottom, request a scroll after layout (e.g. user bubble animation). */
	void NotifyFollowingScrollToBottom();

private:
	/** Coalesces rapid OnStructuralChange bursts (e.g. tool start/end) to one rebuild tick. */
	void ScheduleRebuildTranscript();
	void RebuildTranscript();
	void OnAssistantDeltaUi(const FString& Chunk);
	void OnThinkingDeltaUi(const FString& Chunk, bool bFirstChunk);
	void HandleUserScrolled(float CurrentScrollOffset);
	void OnAssistantRevealTick();
	void ScheduleScrollToEndIfFollowing();
	void ForceScrollToBottomAndFollow();
	EActiveTimerReturnType TickSmoothFollowScroll(double InCurrentTime, float InDeltaTime);
	EVisibility GetJumpToBottomVisibility() const;
	FReply OnJumpToBottomClicked();

	TSharedPtr<FUnrealAiChatTranscript> Transcript;
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<SVerticalBox> MessageBox;

	TSharedPtr<SAssistantStreamBlock> ActiveAssistantWidget;
	TSharedPtr<SThinkingSubline> ActiveThinkingWidget;

	/** User message that plays the slide-in animation on next rebuild. */
	FGuid PendingUserAnimId;

	/** Tool call blocks the user left expanded; preserved across transcript rebuilds. */
	TSet<FGuid> ExpandedToolCallBlockIds;
	/** Run-progress rows that are expanded to show detailed timeline lines. */
	TSet<FGuid> ExpandedRunProgressBlockIds;

	/** SExpandableArea may emit "collapsed" while destroying old rows; ignore during rebuild. */
	bool bSuppressToolExpansionCallbacks = false;

	/** When true, new assistant/thinking content keeps the view pinned to the bottom. */
	bool bStickToBottom = true;

	/** Slate units: treat as "at bottom" if this close to max scroll. */
	static constexpr float StickToBottomThresholdPx = 48.f;

	/** True while actively interpolating scroll offset toward the bottom. */
	bool bSmoothFollowScrollActive = false;
	/** Larger = snappier follow; smaller = smoother drift. */
	static constexpr float FollowScrollInterpSpeed = 11.f;

	bool bTranscriptRebuildPending = false;
	bool bTranscriptRebuildTickerActive = false;
};
