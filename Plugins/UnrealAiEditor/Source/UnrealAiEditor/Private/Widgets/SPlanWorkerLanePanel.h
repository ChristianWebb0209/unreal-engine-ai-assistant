#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/UnrealAiChatTranscript.h"

/** Visual container for a plan DAG node worker: header + body (thinking, assistant, tools). */
class SPlanWorkerLanePanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlanWorkerLanePanel) {}
	SLATE_ARGUMENT(FString, NodeId)
	SLATE_ARGUMENT(FString, TitleDisplay)
	SLATE_ARGUMENT(EUnrealAiPlanWorkerLaneStatus, LaneStatus)
	SLATE_ARGUMENT(FString, SummaryLine)
	/** Activity spinner while the worker LLM round is in flight. */
	SLATE_ARGUMENT(bool, bShowWorkingIndicator)
	/** Estimated prompt footprint vs model profile context (0 = hide bar). */
	SLATE_ARGUMENT(int32, ContextPromptTokensEst)
	SLATE_ARGUMENT(int32, ContextMaxTokens)
	SLATE_DEFAULT_SLOT(FArguments, BodyContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
