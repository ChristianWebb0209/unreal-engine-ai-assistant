#include "Widgets/Plan/SPlanDagWaveList.h"

#include "Widgets/Plan/SPlanNodeRow.h"
#include "Planning/UnrealAiPlanDag.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace
{
	const FUnrealAiDagNode* FindNode(const FUnrealAiPlanDag& Dag, const FString& Id)
	{
		for (const FUnrealAiDagNode& N : Dag.Nodes)
		{
			if (N.Id == Id)
			{
				return &N;
			}
		}
		return nullptr;
	}
}

void SPlanDagWaveList::Construct(const FArguments& InArgs)
{
	const FUnrealAiPlanDag Dag = InArgs._Dag;
	const FUnrealAiPlanNodeStatusMap StatusById = InArgs._NodeStatusById;
	const bool bShowWaveHeaders = InArgs._bShowWaveHeaders;
	const bool bNodesInitiallyCollapsed = InArgs._bNodesInitiallyCollapsed;

	TArray<TArray<FString>> Waves;
	UnrealAiPlanDag::ComputeParallelWaves(Dag, Waves);

	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);
	for (int32 W = 0; W < Waves.Num(); ++W)
	{
		if (bShowWaveHeaders)
		{
			Root->AddSlot().AutoHeight().Padding(FMargin(0.f, W == 0 ? 0.f : 10.f, 0.f, 4.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
						.Font(FUnrealAiEditorStyle::FontLabelBold())
						.Text(FText::Format(
							LOCTEXT("PlanWaveTitle", "Step {0} — parallel-ready"),
							FText::AsNumber(W + 1)))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f, 0.f, 0.f))
				[
					SNew(STextBlock)
						.Font(FUnrealAiEditorStyle::FontCaption())
						.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
						.Text(LOCTEXT(
							"PlanWaveBlurb",
							"These nodes start only after all dependencies from earlier steps are satisfied."))
						.AutoWrapText(true)
				]
			];
		}

		for (const FString& NodeId : Waves[W])
		{
			const FUnrealAiDagNode* N = FindNode(Dag, NodeId);
			if (!N)
			{
				continue;
			}
			const FString* St = StatusById.Find(NodeId);
			const FString StatusLine = St ? *St : FString();
			Root->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				SNew(SPlanNodeRow)
					.Node(*N)
					.StatusLine(StatusLine)
					.bInitiallyCollapsed(bNodesInitiallyCollapsed)
			];
		}
	}

	ChildSlot
		[
			Root
		];
}

#undef LOCTEXT_NAMESPACE
