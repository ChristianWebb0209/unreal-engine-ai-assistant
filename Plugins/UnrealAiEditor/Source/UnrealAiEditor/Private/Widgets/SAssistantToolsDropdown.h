#pragma once

#include "CoreMinimal.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Widgets/SCompoundWidget.h"

/** Combo next to an assistant segment: opens a verbose tool-call log (args, results, ids). */
class SAssistantToolsDropdown final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssistantToolsDropdown) {}
	SLATE_ARGUMENT(TArray<FUnrealAiAssistantSegmentToolInfo>, ToolDetails)
	SLATE_ARGUMENT(FGuid, RunId)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
