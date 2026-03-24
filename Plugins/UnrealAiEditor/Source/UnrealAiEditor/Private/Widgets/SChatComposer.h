#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "Composer/UnrealAiComposerMentionIndex.h"
#include "Composer/UnrealAiComposerPromptResolver.h"
#include "Context/AgentContextTypes.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

struct FSlateBrush;

class FUnrealAiBackendRegistry;
class FUnrealAiOrchestrateExecutor;
class SChatMessageList;
struct FUnrealAiChatUiSession;

class SChatComposer final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatComposer) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_ARGUMENT(TSharedPtr<SChatMessageList>, MessageList)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiChatUiSession>, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SChatComposer() override;
	void SyncAttachmentChipsUi();
	void ResetComposerInput();

private:
	FReply OnSendOrStopClicked();
	FReply OnSendClicked();
	FReply OnAttachScreenshotClicked();
	FReply OnNewChatClicked();
	FText GetFooterText() const;

	void OnModelSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void RestoreModelComboSelection();
	TSharedPtr<FString> FindModelOptionMatchingSession() const;
	FText GetSelectedModelText() const;
	bool IsModelComboEnabled() const;

	TSharedRef<SWidget> BuildModeMenuContent();
	TSharedRef<SWidget> MakeModeMenuRow(EUnrealAiAgentMode Mode, FText Title, FText Blurb);
	void SetAgentMode(EUnrealAiAgentMode NewMode);
	FText GetModeLabelShort() const;
	FLinearColor GetModeAccent() const;
	const FSlateBrush* GetModeIconBrush() const;

	bool IsHarnessTurnInProgress() const;
	const FSlateBrush* GetSendStopBrush() const;
	FText GetSendStopLabel() const;
	FText GetSendStopTooltip() const;
	EActiveTimerReturnType OnComposerRefreshTick(double InCurrentTime, float InDeltaTime);

	FReply OnInputKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);
	void OnPromptTextChanged(const FText& NewText);
	void RefreshAttachmentChips();
	FReply OnRemoveAttachment(int32 Index);
	void UpdateMentionSuggestions(const FString& FullText);
	void RebuildMentionPanelUi();
	void UpdateSlashCommandSuggestions(const FString& FullText);
	void RebuildSlashPanelUi();
	void ScheduleComboScrollIntoView(bool bSlashPanel);
	EActiveTimerReturnType FlushPendingComboScroll(double InCurrentTime, float InDeltaTime);
	FReply OnPickMention(const FString& AssetPath);
	FReply OnPickMentionCandidate(const FMentionCandidate& Candidate);
	FReply OnPickSlashCommand(const FString& Command);

	bool IsMentionMenuOpenWithChoices() const;
	bool IsSlashMenuOpenWithChoices() const;
	bool IsMentionPanelBlockingEnterWhileBuilding() const;

	void OnViewportScreenshotProcessed();
	void UnbindViewportScreenshotProcessed();

	TSharedPtr<class SBox> InputBoxWrap;
	TSharedPtr<class SMultiLineEditableTextBox> InputBox;
	TSharedPtr<class SWrapBox> ChipsRow;
	TSharedPtr<class SBorder> MentionBorder;
	TSharedPtr<class SScrollBox> MentionScrollBox;
	TSharedPtr<class SVerticalBox> MentionListBox;
	TSharedPtr<class SBorder> SlashCommandBorder;
	TSharedPtr<class SScrollBox> SlashScrollBox;
	TSharedPtr<class SVerticalBox> SlashCommandListBox;
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<SChatMessageList> MessageList;
	TSharedPtr<FUnrealAiChatUiSession> Session;

	TArray<TSharedPtr<FString>> ModelOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ModelCombo;

	EUnrealAiAgentMode AgentMode = EUnrealAiAgentMode::Agent;

	FDelegateHandle ContextAttachmentsChangedHandle;

	/** Minimum height of the input area (stable when hint hides; grows with content up to InputMaxHeight). */
	static constexpr float InputMinHeight = 72.f;
	static constexpr float InputMaxHeight = 260.f;
	FString PendingViewportScreenshotPath;
	FDelegateHandle ViewportScreenshotProcessedHandle;
	bool bLastHarnessTurnBusy = false;

	TArray<FMentionCandidate> MentionFilteredList;
	TArray<FUnrealAiSlashCommand> SlashFilteredList;
	int32 MentionSelectedIndex = 0;
	int32 SlashSelectedIndex = 0;

	TWeakPtr<class SScrollBox> PendingComboScrollBox;
	TWeakPtr<class SWidget> PendingComboScrollRow;
	TSharedPtr<FUnrealAiOrchestrateExecutor> ActiveOrchestrateExecutor;
};
