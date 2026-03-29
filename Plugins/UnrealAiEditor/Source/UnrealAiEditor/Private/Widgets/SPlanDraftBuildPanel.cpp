#include "Widgets/SPlanDraftBuildPanel.h"

#include "Misc/UnrealAiWaitTimePolicy.h"
#include "Planning/UnrealAiPlanDag.h"
#include "Widgets/Plan/SPlanDagWaveList.h"
#include "Widgets/Plan/SPlanJsonValidationBanner.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Styling/AppStyle.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Widgets/UnrealAiPlanDraftPersist.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SPlanDraftBuildPanel::Construct(const FArguments& InArgs)
{
	DraftText = InArgs._InitialDagJson;
	BlockId = InArgs._BlockId;
	BackendRegistry = InArgs._BackendRegistry;
	Session = InArgs._Session;
	Transcript = InArgs._Transcript;
	ProjectId = InArgs._ProjectId;
	ThreadId = InArgs._ThreadId;

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.AssistantLane")))
				.Padding(FMargin(10.f, 10.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
					[
						SNew(STextBlock)
							.Text(LOCTEXT(
								"PlanDraftHint",
								"Review the structured plan, edit JSON if needed, then Build. Edits save per thread."))
							.Font(FUnrealAiEditorStyle::FontRegular10())
							.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
							.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
					[
						SNew(SBox)
							.MaxDesiredHeight(280.f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot().Padding(0.f)
								[
									SAssignNew(PreviewHost, SBox)
								]
							]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
					[
						SAssignNew(ValidationBanner, SPlanJsonValidationBanner)
							.Message(FString())
							.bIsError(true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
					[
						SAssignNew(ShowJsonCheck, SCheckBox)
							.Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
							.IsChecked(bShowJsonEditor ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
							.OnCheckStateChanged(this, &SPlanDraftBuildPanel::OnShowJsonToggled)
							[
								SNew(STextBlock)
									.Font(FUnrealAiEditorStyle::FontBodySmall())
									.Text(LOCTEXT("PlanDraftShowJson", "Show JSON editor"))
							]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
					[
						SNew(SBox)
							.Visibility_Lambda([this]()
							{
								return bShowJsonEditor ? EVisibility::Visible : EVisibility::Collapsed;
							})
							.MinDesiredHeight(140.f)
							[
								SAssignNew(DraftEdit, SMultiLineEditableTextBox)
									.Text(FText::FromString(DraftText))
									.Font(FUnrealAiEditorStyle::FontMono9())
									.OnTextChanged(this, &SPlanDraftBuildPanel::OnDraftTextChanged)
							]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
							.IsEnabled_Lambda([this]() { return bDraftValid; })
							.OnClicked(this, &SPlanDraftBuildPanel::OnBuildClicked)
							.ToolTipText(LOCTEXT("PlanDraftBuildTip", "Run plan execution (requires valid DAG)."))
							[
								SNew(STextBlock)
									.Font(FUnrealAiEditorStyle::FontComposerBadge())
									.Text(LOCTEXT("PlanDraftBuild", "Build"))
							]
					]
				]
		];

	RefreshPlanUi();
}

void SPlanDraftBuildPanel::RefreshPlanUi()
{
	if (!PreviewHost.IsValid() || !ValidationBanner.IsValid())
	{
		return;
	}

	FUnrealAiPlanDag Dag;
	FString ParseErr;
	if (!UnrealAiPlanDag::ParseDagJson(DraftText, Dag, ParseErr) || Dag.Nodes.Num() == 0)
	{
		bDraftValid = false;
		ValidationBanner->SetMessage(ParseErr.IsEmpty()
				? FString(TEXT("Enter valid unreal_ai.plan_dag JSON (non-empty nodes[])."))
				: ParseErr,
			true);
		PreviewHost->SetContent(
			SNew(STextBlock)
				.AutoWrapText(true)
				.Font(FUnrealAiEditorStyle::FontRegular10())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("PlanDraftNoPreview", "Fix JSON to show structured preview.")));
		return;
	}

	FString ValErr;
	if (!UnrealAiPlanDag::ValidateDag(Dag, UnrealAiWaitTime::PlannerEmittedMaxDagNodes, ValErr))
	{
		bDraftValid = false;
		ValidationBanner->SetMessage(ValErr, true);
		PreviewHost->SetContent(
			SNew(STextBlock)
				.AutoWrapText(true)
				.Font(FUnrealAiEditorStyle::FontRegular10())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.55f, 0.45f, 1.f)))
				.Text(FText::FromString(FString::Printf(TEXT("DAG invalid: %s"), *ValErr))));
		return;
	}

	bDraftValid = true;
	ValidationBanner->SetMessage(FString(), false);
	PreviewHost->SetContent(
		SNew(SPlanDagWaveList)
			.Dag(Dag)
			.NodeStatusById(FUnrealAiPlanNodeStatusMap())
			.bShowWaveHeaders(true)
			.bNodesInitiallyCollapsed(false));
}

void SPlanDraftBuildPanel::OnShowJsonToggled(const ECheckBoxState State)
{
	bShowJsonEditor = (State == ECheckBoxState::Checked);
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SPlanDraftBuildPanel::OnDraftTextChanged(const FText& NewText)
{
	DraftText = NewText.ToString();
	if (Transcript.IsValid() && BlockId.IsValid())
	{
		Transcript->SetPlanDraftJsonForBlock(BlockId, DraftText);
	}
	if (!BackendRegistry.IsValid() || ProjectId.IsEmpty() || ThreadId.IsEmpty())
	{
		RefreshPlanUi();
		return;
	}
	if (IUnrealAiPersistence* P = BackendRegistry->GetPersistence())
	{
		P->SaveThreadPlanDraftJson(ProjectId, ThreadId, UnrealAiPlanDraftPersist::WrapDraftFile(DraftText));
	}
	RefreshPlanUi();
}

FReply SPlanDraftBuildPanel::OnBuildClicked()
{
	if (!bDraftValid)
	{
		RefreshPlanUi();
		return FReply::Handled();
	}
	if (DraftEdit.IsValid())
	{
		DraftText = DraftEdit->GetText().ToString();
		RefreshPlanUi();
		if (!bDraftValid)
		{
			return FReply::Handled();
		}
	}
	if (Session.IsValid())
	{
		Session->OnPlanDraftBuild.Broadcast(DraftText);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
