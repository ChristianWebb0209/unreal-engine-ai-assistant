#include "Widgets/Plan/SPlanNodeRow.h"

#include "Widgets/Plan/SPlanNodeStatusBadge.h"
#include "Widgets/Plan/UnrealAiPlanUiTokens.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Styling/AppStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace
{
	TSharedRef<SWidget> MakeDependsOnWrap(const TArray<FString>& Deps)
	{
		if (Deps.Num() == 0)
		{
			return SNew(SSpacer);
		}
		TSharedRef<SWrapBox> Box =
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f));
		for (const FString& Dep : Deps)
		{
			Box->AddSlot()
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
					.BorderBackgroundColor(FLinearColor(0.14f, 0.15f, 0.2f, 0.9f))
					.Padding(FMargin(6.f, 2.f))
					[
						SNew(STextBlock)
							.Font(FUnrealAiEditorStyle::FontMono8())
							.Text(FText::FromString(Dep))
					]
			];
		}
		return Box;
	}
}

void SPlanNodeRow::Construct(const FArguments& InArgs)
{
	Node = InArgs._Node;
	StatusLine = InArgs._StatusLine;
	const bool bInitiallyCollapsed = InArgs._bInitiallyCollapsed;

	const FString TitleText = Node.Title.IsEmpty() ? Node.Id : Node.Title;
	const FLinearColor Rail = FUnrealAiPlanUiTokens::PlanAccent();

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.ToolCallCardOuter")))
				.Padding(FMargin(0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
					[
						SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
							.BorderBackgroundColor(FSlateColor(Rail))
							[
								SNew(SBox).WidthOverride(4.f)[SNew(SSpacer)]
							]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SExpandableArea)
							.InitiallyCollapsed(bInitiallyCollapsed)
							.AreaTitle(FText::GetEmpty())
							.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
							.BorderBackgroundColor(FUnrealAiEditorStyle::LinearColorToolCallCardInset())
							.HeaderContent()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.f, 4.f, 6.f, 4.f))
								[
									SNew(SPlanNodeStatusBadge).StatusRaw(StatusLine.IsEmpty() ? TEXT("pending") : StatusLine)
								]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
								[
									SNew(SBorder)
										.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
										.BorderBackgroundColor(FLinearColor(0.12f, 0.13f, 0.16f, 0.95f))
										.Padding(FMargin(6.f, 2.f))
										[
											SNew(STextBlock)
												.Font(FUnrealAiEditorStyle::FontMono9())
												.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.82f, 0.78f, 1.f)))
												.Text(FText::FromString(Node.Id))
										]
								]
								+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(0.f, 4.f, 6.f, 4.f))
								[
									SNew(STextBlock)
										.Font(FUnrealAiEditorStyle::FontListRowTitle())
										.AutoWrapText(true)
										.Text(FText::FromString(TitleText))
								]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(4.f, 4.f, 6.f, 4.f))
								[
									SNew(SButton)
										.ButtonStyle(FAppStyle::Get(), "SimpleButton")
										.OnClicked(this, &SPlanNodeRow::OnCopyClicked)
										.ToolTipText(LOCTEXT("PlanNodeCopyTip", "Copy node summary"))
										[
											SNew(STextBlock)
												.Font(FUnrealAiEditorStyle::FontCaption())
												.Text(LOCTEXT("PlanNodeCopy", "Copy"))
										]
								]
							]
							.BodyContent()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.f, 0.f, 10.f, 6.f))
								[
									SNew(STextBlock)
										.Visibility(Node.Hint.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
										.Font(FUnrealAiEditorStyle::FontRegular10())
										.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
										.AutoWrapText(true)
										.Text(FText::FromString(Node.Hint))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.f, 0.f, 10.f, 8.f))
								[
									SNew(SHorizontalBox)
									.Visibility(Node.DependsOn.Num() == 0 ? EVisibility::Collapsed : EVisibility::Visible)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
									[
										SNew(STextBlock)
											.Font(FUnrealAiEditorStyle::FontCaption())
											.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMetaHint())
											.Text(LOCTEXT("PlanDepsLabel", "Depends on:"))
									]
									+ SHorizontalBox::Slot().FillWidth(1.f)
									[
										MakeDependsOnWrap(Node.DependsOn)
									]
								]
							]
					]
				]
		];
}

FReply SPlanNodeRow::OnCopyClicked()
{
	const FString Body = FString::Printf(
		TEXT("id: %s\nstatus: %s\ntitle: %s\nhint: %s"),
		*Node.Id,
		StatusLine.IsEmpty() ? TEXT("pending") : *StatusLine,
		*Node.Title,
		*Node.Hint);
	FPlatformApplicationMisc::ClipboardCopy(*Body);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
