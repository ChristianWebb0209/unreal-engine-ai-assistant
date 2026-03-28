#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;
class FUnrealAiChatTranscript;

/** Plan DAG JSON editor + Build CTA (Plan mode, awaiting user Build). */
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

	FString DraftText;
	FGuid BlockId;
	TSharedPtr<class SMultiLineEditableTextBox> DraftEdit;
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	TSharedPtr<FUnrealAiChatTranscript> Transcript;
	FString ProjectId;
	FString ThreadId;
};
