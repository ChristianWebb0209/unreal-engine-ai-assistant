#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;
class SMultiLineEditableTextBox;

class SUnrealAiEditorSettingsTab final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAiEditorSettingsTab) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnSaveClicked();
	FReply OnOpenApiModelsTab();
	void LoadSettingsTextIntoBox();

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<SMultiLineEditableTextBox> SettingsJsonBox;
};
