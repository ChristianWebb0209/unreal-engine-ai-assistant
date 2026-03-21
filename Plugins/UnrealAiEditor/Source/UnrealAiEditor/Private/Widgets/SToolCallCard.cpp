#include "Widgets/SToolCallCard.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "Styling/CoreStyle.h"
#include "Widgets/UnrealAiToolUi.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SToolCallCard::RegisterPulseTimerIfNeeded()
{
	if (!bRunning)
	{
		return;
	}
	RegisterActiveTimer(
		0.05f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SToolCallCard::PulseTimerTick));
}

EActiveTimerReturnType SToolCallCard::PulseTimerTick(double InCurrentTime, float InDeltaTime)
{
	if (!bRunning)
	{
		return EActiveTimerReturnType::Stop;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	return EActiveTimerReturnType::Continue;
}

FReply SToolCallCard::OnCopyClicked()
{
	const FString Combined = FString::Printf(
		TEXT("Tool: %s\nArgs: %s\nResult: %s"),
		*ToolName,
		*ArgsPreview,
		*ResultPreview);
	FPlatformApplicationMisc::ClipboardCopy(*Combined);
	return FReply::Handled();
}

void SToolCallCard::Construct(const FArguments& InArgs)
{
	ToolName = InArgs._ToolName;
	ArgsPreview = InArgs._ArgumentsPreview;
	ResultPreview = InArgs._ResultPreview;
	bRunning = InArgs._bRunning;
	bSuccess = InArgs._bSuccess;

	const EUnrealAiToolVisualCategory Cat = UnrealAiClassifyToolVisuals(ToolName);
	CategoryTint = UnrealAiToolCategoryTint(Cat);
	const FLinearColor DotColor = CategoryTint;

	ChildSlot
		[SNew(SBorder)
				.BorderBackgroundColor_Lambda([this, BaseTint = CategoryTint]() -> FSlateColor
				{
					if (!bRunning)
					{
						return FSlateColor(BaseTint);
					}
					const float T = FMath::Abs(FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 3.2f));
					const FLinearColor Dim = BaseTint * FLinearColor(0.42f, 0.42f, 0.42f, 0.55f);
					const FLinearColor Bright = BaseTint * FLinearColor(0.95f, 0.95f, 0.95f, 0.95f);
					return FSlateColor(FLinearColor::LerpUsingHSV(Dim, Bright, T));
				})
				.Padding(FMargin(4.f))
				[
					SNew(SExpandableArea)
						.InitiallyCollapsed(true)
						.AreaTitle(FText::GetEmpty())
						.BorderBackgroundColor(FLinearColor(0.09f, 0.09f, 0.1f, 0.98f))
						.HeaderContent()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
							[
								SNew(SBox)
									.WidthOverride(3.f)
									.HeightOverride(18.f)
									[
										SNew(SBorder)
											.BorderBackgroundColor(DotColor)
											[
												SNew(SSpacer)
											]
									]
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 4.f, 0.f))
							[
								SNew(STextBlock)
									.Text(FText::FromString(TEXT("\u2022")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
									.ColorAndOpacity(FSlateColor(DotColor))
							]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
									.Text(FText::FromString(ToolName))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.f)))
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(4.f, 0.f, 6.f, 0.f))
							[
								SNew(SButton)
									.ButtonStyle(FCoreStyle::Get(), "NoBorder")
									.ContentPadding(FMargin(4.f, 2.f))
									.Text(LOCTEXT("CopyTool", "Copy"))
									.OnClicked(this, &SToolCallCard::OnCopyClicked)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(STextBlock)
									.Text_Lambda([this]()
									{
										if (bRunning)
										{
											return LOCTEXT("ToolRunning", "Running…");
										}
										return bSuccess ? LOCTEXT("ToolOk", "Done") : LOCTEXT("ToolFail", "Failed");
									})
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
									.ColorAndOpacity_Lambda([this]()
									{
										if (bRunning)
										{
											return FSlateColor(FLinearColor(0.9f, 0.85f, 0.4f, 1.f));
										}
										return bSuccess
											? FSlateColor(FLinearColor(0.45f, 0.9f, 0.5f, 1.f))
											: FSlateColor(FLinearColor(0.95f, 0.4f, 0.4f, 1.f));
									})
							]
						]
						.BodyContent()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 2.f, 4.f, 2.f))
							[
								SNew(STextBlock)
									.Text(LOCTEXT("ArgsHdr", "Arguments (JSON)"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.7f, 0.78f, 1.f)))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 0.f, 4.f, 4.f))
							[
								SNew(SBox)
									.MinDesiredWidth(200.f)
									.MaxDesiredHeight(160.f)
									[
										SNew(SMultiLineEditableText)
											.IsReadOnly(true)
											.AutoWrapText(true)
											.Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
											.Text(FText::FromString(ArgsPreview.IsEmpty() ? TEXT("(empty)") : ArgsPreview))
									]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 0.f, 4.f, 2.f))
							[
								SNew(STextBlock)
									.Text(LOCTEXT("ResHdr", "Result"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.7f, 0.78f, 1.f)))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 0.f, 4.f, 2.f))
							[
								SNew(SBox)
									.MinDesiredWidth(200.f)
									.MaxDesiredHeight(200.f)
									[
										SNew(SMultiLineEditableText)
											.IsReadOnly(true)
											.AutoWrapText(true)
											.Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
											.Text(FText::FromString(
												ResultPreview.IsEmpty() && bRunning ? FString(TEXT("…")) : ResultPreview))
									]
							]
						]
				]];

	RegisterPulseTimerIfNeeded();
}

void SToolCallCard::SetFinished(bool bOk, const FString& InResultPreview)
{
	bRunning = false;
	bSuccess = bOk;
	ResultPreview = InResultPreview;
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

#undef LOCTEXT_NAMESPACE
