#include "Widgets/SPlanDraftBuildPanel.h"

#include "Widgets/UnrealAiChatTranscript.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Widgets/UnrealAiPlanDraftPersist.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

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
				.BorderBackgroundColor(FLinearColor(0.12f, 0.14f, 0.18f, 0.95f))
				.Padding(FMargin(10.f, 10.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
					[
						SNew(STextBlock)
							.Text(LOCTEXT("PlanDraftHint", "Review or edit the plan JSON, then click Build to run nodes."))
							.Font(FUnrealAiEditorStyle::FontRegular10())
							.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
							.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 0.f, 0.f, 8.f)
					[
						SNew(SBox)
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
							.OnClicked(this, &SPlanDraftBuildPanel::OnBuildClicked)
							[
								SNew(STextBlock)
									.Font(FUnrealAiEditorStyle::FontComposerBadge())
									.Text(LOCTEXT("PlanDraftBuild", "Build"))
							]
					]
				]
		];
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
		return;
	}
	if (IUnrealAiPersistence* P = BackendRegistry->GetPersistence())
	{
		P->SaveThreadPlanDraftJson(ProjectId, ThreadId, UnrealAiPlanDraftPersist::WrapDraftFile(DraftText));
	}
}

FReply SPlanDraftBuildPanel::OnBuildClicked()
{
	if (DraftEdit.IsValid())
	{
		DraftText = DraftEdit->GetText().ToString();
	}
	if (Session.IsValid())
	{
		Session->OnPlanDraftBuild.Broadcast(DraftText);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
