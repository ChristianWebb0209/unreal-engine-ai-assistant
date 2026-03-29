#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;
class FUnrealAiChatTranscript;
class SBox;
class SCheckBox;
class SMultiLineEditableTextBox;
class SPlanJsonValidationBanner;

/** Plan DAG JSON editor + structured preview + Build CTA (Plan mode, awaiting user Build). */
class SPlanDraftBuildPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlanDraftBuildPanel) {}
	SLATE_ARGUMENT(FString, InitialDagJson)
	SLATE_ARGUMENT(FGuid, BlockId)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiChatUiSession>, Session)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiChatTranscript>, Transcript)
	SLATE_ARGUMENT(FString, ProjectId)
	SLATE_ARGUMENT(FString, ThreadId)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnBuildClicked();
	void OnDraftTextChanged(const FText& NewText);
	void OnShowJsonToggled(ECheckBoxState State);
	void RefreshPlanUi();

	FString DraftText;
	FGuid BlockId;
	TSharedPtr<SMultiLineEditableTextBox> DraftEdit;
	TSharedPtr<SBox> PreviewHost;
	TSharedPtr<SPlanJsonValidationBanner> ValidationBanner;
	TSharedPtr<SCheckBox> ShowJsonCheck;
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	TSharedPtr<FUnrealAiChatTranscript> Transcript;
	FString ProjectId;
	FString ThreadId;
	bool bDraftValid = false;
	bool bShowJsonEditor = true;
};
