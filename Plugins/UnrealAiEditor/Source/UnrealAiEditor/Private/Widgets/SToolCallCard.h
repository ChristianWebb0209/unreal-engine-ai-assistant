#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

#include "Tools/Presentation/UnrealAiToolEditorPresentation.h"

/** Inline tool call row with collapsible args/result and copy. */
class SToolCallCard final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SToolCallCard)
		{}
	SLATE_ARGUMENT(FString, ToolName)
	SLATE_ARGUMENT(FString, ArgumentsPreview)
	SLATE_ARGUMENT(FString, ResultPreview)
	SLATE_ARGUMENT(bool, bRunning)
	SLATE_ARGUMENT(bool, bSuccess)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiToolEditorPresentation>, EditorPresentation)
	SLATE_ARGUMENT(bool, bInitiallyCollapsed)
	SLATE_EVENT(FOnBooleanValueChanged, OnExpansionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetFinished(bool bOk, const FString& ResultPreview);

private:
	void HandleExpansionChanged(bool bIsExpanded);
	void RegisterPulseTimerIfNeeded();
	EActiveTimerReturnType PulseTimerTick(double InCurrentTime, float InDeltaTime);
	FReply OnCopyClicked();

	FString ToolName;
	FString ArgsPreview;
	FString ResultPreview;
	bool bRunning = true;
	bool bSuccess = false;
	FLinearColor CategoryTint = FLinearColor::White;
	TSharedPtr<FUnrealAiToolEditorPresentation> EditorPresentation;
	FOnBooleanValueChanged OnExpansionChanged;
};
