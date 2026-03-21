#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;

class SUnrealAiEditorApiModelsTab final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAiEditorApiModelsTab) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnTestConnection();
	FReply OnSave();
	void OnMaskToggled(ECheckBoxState NewState);

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<class SEditableTextBox> ProviderBox;
	TSharedPtr<class SEditableTextBox> BaseUrlBox;
	TSharedPtr<class SEditableTextBox> ApiKeyBox;
	TSharedPtr<class SEditableTextBox> ModelBox;
	bool bMaskKey = true;
	FText StatusText;
};
