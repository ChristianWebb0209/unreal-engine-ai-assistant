#pragma once

#include "CoreMinimal.h"
#include "Planning/UnrealAiPlanDag.h"
#include "Widgets/SCompoundWidget.h"

class SPlanNodeStatusBadge;

/** One plan DAG node: collapsible hint + dependencies + copy. */
class SPlanNodeRow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlanNodeRow) {}
	SLATE_ARGUMENT(FUnrealAiDagNode, Node)
	/** Executor status string (e.g. success, running, skipped: …). Empty => pending. */
	SLATE_ARGUMENT(FString, StatusLine)
	SLATE_ARGUMENT(bool, bInitiallyCollapsed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnCopyClicked();

	FUnrealAiDagNode Node;
	FString StatusLine;
};
