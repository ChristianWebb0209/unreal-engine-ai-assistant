#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SUnrealAiEditorHelpTab final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAiEditorHelpTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnOpenDocsFolder();
	FReply OnOpenLogsFolder();
};
