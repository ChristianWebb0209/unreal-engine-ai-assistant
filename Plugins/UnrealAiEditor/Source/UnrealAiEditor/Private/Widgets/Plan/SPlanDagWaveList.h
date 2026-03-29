#pragma once

#include "CoreMinimal.h"
#include "Planning/UnrealAiPlanDag.h"
#include "Widgets/SCompoundWidget.h"

using FUnrealAiPlanNodeStatusMap = TMap<FString, FString>;

/** Renders a plan DAG grouped into static execution waves with collapsible node rows. */
class SPlanDagWaveList final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlanDagWaveList) {}
	SLATE_ARGUMENT(FUnrealAiPlanDag, Dag)
	SLATE_ARGUMENT(FUnrealAiPlanNodeStatusMap, NodeStatusById)
	SLATE_ARGUMENT(bool, bShowWaveHeaders)
	/** Collapsed body for each node row (hint/deps). */
	SLATE_ARGUMENT(bool, bNodesInitiallyCollapsed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
