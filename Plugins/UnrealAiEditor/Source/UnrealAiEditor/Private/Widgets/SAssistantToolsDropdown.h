#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/** Compact combo next to an assistant segment listing tool names in call order (colored bullets). */
class SAssistantToolsDropdown final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssistantToolsDropdown) {}
	SLATE_ARGUMENT(TArray<FString>, ToolNames)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
