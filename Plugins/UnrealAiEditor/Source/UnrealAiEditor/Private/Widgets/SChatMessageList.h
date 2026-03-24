#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"

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

	/** @deprecated Prefer AddUserMessage; kept for call sites. */
	void ResetAssistant();

	/** Called when the user message entry animation finishes. */
	void ClearUserAnimId();

	/** While following the bottom, request a scroll after layout (e.g. user bubble animation). */
	void NotifyFollowingScrollToBottom();

private:
	void RebuildTranscript();
	void OnAssistantDeltaUi(const FString& Chunk);
	void OnThinkingDeltaUi(const FString& Chunk, bool bFirstChunk);
	void HandleUserScrolled(float CurrentScrollOffset);
	void OnAssistantRevealTick();
	void ScheduleScrollToEndIfFollowing();
	void ForceScrollToBottomAndFollow();
	EActiveTimerReturnType FlushPendingScrollToEnd(double InCurrentTime, float InDeltaTime);
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

	/** When true, new assistant/thinking content keeps the view pinned to the bottom. */
	bool bStickToBottom = true;

	/** Slate units: treat as "at bottom" if this close to max scroll. */
	static constexpr float StickToBottomThresholdPx = 48.f;

	bool bScrollToEndTimerPending = false;
};
