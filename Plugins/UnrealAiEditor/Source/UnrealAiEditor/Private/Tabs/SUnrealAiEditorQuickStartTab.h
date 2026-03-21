#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SUnrealAiEditorQuickStartTab final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAiEditorQuickStartTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnOpenChat();
};
