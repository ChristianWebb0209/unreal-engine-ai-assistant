#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;
class SChatMessageList;
struct FUnrealAiChatUiSession;

class SChatComposer final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatComposer) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_ARGUMENT(TSharedPtr<SChatMessageList>, MessageList)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiChatUiSession>, Session)
	SLATE_EVENT(FSimpleDelegate, OnNewChat)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SyncAttachmentChipsUi();

private:
	FReply OnSendClicked();
	FReply OnAttachClicked();
	FReply OnNewChatClicked();
	FReply OnCycleModeClicked();
	FText GetModeButtonText() const;
	FText GetFooterText() const;

	FReply OnInputKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);
	void OnPromptTextChanged(const FText& NewText);
	void RefreshAttachmentChips();
	FReply OnRemoveAttachment(int32 Index);
	void UpdateMentionSuggestions(const FString& FullText);
	FReply OnPickMention(const FString& AssetPath);

	TSharedPtr<class SMultiLineEditableTextBox> InputBox;
	TSharedPtr<class SVerticalBox> ChipsRow;
	TSharedPtr<class SBorder> MentionBorder;
	TSharedPtr<class SVerticalBox> MentionListBox;
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<SChatMessageList> MessageList;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	FSimpleDelegate OnNewChatParent;

	EUnrealAiAgentMode AgentMode = EUnrealAiAgentMode::Fast;
};
