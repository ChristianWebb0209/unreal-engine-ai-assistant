#include "Widgets/SPlanWorkerLanePanel.h"

#include "Style/UnrealAiEditorStyle.h"
#include "Widgets/Plan/UnrealAiPlanUiTokens.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSpacer.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SPlanWorkerLanePanel::Construct(const FArguments& InArgs)
{
	const FLinearColor Accent = FUnrealAiPlanUiTokens::PlanAccent();
	FText StatusText;
	switch (InArgs._LaneStatus)
	{
	case EUnrealAiPlanWorkerLaneStatus::Running:
		StatusText = LOCTEXT("PlanWorkerLaneRunning", "Working…");
		break;
	case EUnrealAiPlanWorkerLaneStatus::Succeeded:
		StatusText = LOCTEXT("PlanWorkerLaneDone", "Done");
		break;
	case EUnrealAiPlanWorkerLaneStatus::Failed:
		StatusText = LOCTEXT("PlanWorkerLaneFailed", "Failed");
		break;
	default:
		StatusText = LOCTEXT("PlanWorkerLaneUnknown", "…");
		break;
	}

	const FSlateColor StatusColor =
		InArgs._LaneStatus == EUnrealAiPlanWorkerLaneStatus::Failed
			? FSlateColor(FUnrealAiEditorStyle::LinearColorChatTranscriptAccentNoticeError())
			: (InArgs._LaneStatus == EUnrealAiPlanWorkerLaneStatus::Succeeded
				  ? FUnrealAiEditorStyle::ColorTextPrimary()
				  : FSlateColor(Accent));

	const TSharedRef<SWidget> SummaryWidget = InArgs._SummaryLine.IsEmpty()
		? StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)
		: StaticCastSharedRef<SWidget>(
			  SNew(SBox)
				  .Padding(FMargin(0.f, 0.f, 0.f, 4.f))
				  [
					  SNew(STextBlock)
						  .AutoWrapText(true)
						  .WrapTextAt(720.f)
						  .Font(FUnrealAiEditorStyle::FontBodySmall())
						  .ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
						  .Text(FText::FromString(InArgs._SummaryLine))
				  ]);

	const bool bShowCtx = InArgs._ContextMaxTokens > 0;
	const float CtxPct = bShowCtx
		? FMath::Clamp(
			  static_cast<float>(InArgs._ContextPromptTokensEst) / static_cast<float>(InArgs._ContextMaxTokens),
			  0.f,
			  1.f)
		: 0.f;
	const FText CtxTooltip = bShowCtx
		? FText::Format(
			  LOCTEXT(
				  "PlanWorkerCtxTooltip",
				  "Estimated size of the outbound request (prompt + tools, approximate): ~{0} tokens.\n"
				  "Model context window from the active profile: {1} tokens."),
			  FText::AsNumber(InArgs._ContextPromptTokensEst),
			  FText::AsNumber(InArgs._ContextMaxTokens))
		: FText::GetEmpty();

	const TSharedRef<SWidget> ContextRow = bShowCtx
		? StaticCastSharedRef<SWidget>(
			  SNew(SBorder)
				  .BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
				  .Padding(FMargin(0.f, 0.f, 0.f, 6.f))
				  .ToolTipText(CtxTooltip)
				  [
					  SNew(SVerticalBox)
					  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
					  [
						  SNew(SProgressBar)
							  .Percent(CtxPct)
							  .FillColorAndOpacity(Accent)
					  ]
					  + SVerticalBox::Slot().AutoHeight()
					  [
						  SNew(STextBlock)
							  .Font(FUnrealAiEditorStyle::FontCaption())
							  .ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
							  .Text(FText::Format(
								  LOCTEXT("PlanWorkerCtxLine", "Context (est.): ~{0} / {1} tok"),
								  FText::AsNumber(InArgs._ContextPromptTokensEst),
								  FText::AsNumber(InArgs._ContextMaxTokens)))
					  ]
				  ])
		: StaticCastSharedRef<SWidget>(SNullWidget::NullWidget);

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.Elevated")))
				.Padding(FMargin(0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f)
					[
						SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
							.BorderBackgroundColor(Accent)
							.Padding(FMargin(0.f))
							[
								SNew(SBox).WidthOverride(4.f)
								[
									SNew(SSpacer)
								]
							]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(10.f, 8.f, 10.f, 8.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
							[
								SNew(SImage)
									.ColorAndOpacity(Accent)
									.Image(FAppStyle::GetBrush(TEXT("Icons.Edit")))
							]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(STextBlock)
										.Font(FUnrealAiEditorStyle::FontLabelBold())
										.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextPrimary())
										.Text(FText::FromString(InArgs._TitleDisplay))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
								[
									SNew(STextBlock)
										.Font(FUnrealAiEditorStyle::FontCaption())
										.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
										.Text(FText::Format(
											LOCTEXT("PlanWorkerLaneIdFmt", "Node: {0}"),
											FText::FromString(InArgs._NodeId)))
								]
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 4.f, 0.f)
							[
								SNew(SBox)
									.Visibility(
										InArgs._bShowWorkingIndicator ? EVisibility::Visible : EVisibility::Collapsed)
								[
									SNew(SCircularThrobber)
										.Radius(7.f)
								]
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(2.f, 0.f, 0.f, 0.f)
							[
								SNew(STextBlock)
									.Font(FUnrealAiEditorStyle::FontCaption())
									.ColorAndOpacity(StatusColor)
									.Text(StatusText)
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							ContextRow
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SummaryWidget
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							InArgs._BodyContent.Widget
						]
					]
				]
		];
}

#undef LOCTEXT_NAMESPACE
