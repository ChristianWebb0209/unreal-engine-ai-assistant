#include "Widgets/SToolCallCard.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SHorizontalBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SVerticalBox.h"
#include "Widgets/Text/STextBlock.h"
#include "HAL/PlatformApplication.h"
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
	FPlatformApplication::ClipboardCopy(*Combined);
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

	const FString ArgsUi = UnrealAiTruncateForUi(ArgsPreview);
	const FString ResUi = UnrealAiTruncateForUi(ResultPreview);

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
				.Padding(FMargin(6.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(TEXT("\u2022")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
								.ColorAndOpacity(FSlateColor(DotColor))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(TEXT("Tool: %s"), *ToolName)))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.f)))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
						[
							SNew(SButton)
								.Text(LOCTEXT("CopyTool", "Copy"))
								.OnClicked(this, &SToolCallCard::OnCopyClicked)
						]
						+ SHorizontalBox::Slot().AutoWidth()
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
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
					[
						SNew(SExpandableArea)
							.InitiallyCollapsed(true)
							.BorderBackgroundColor(FLinearColor(0.118f, 0.118f, 0.118f, 1.f))
							.HeaderContent()
							[
								SNew(STextBlock).Text(LOCTEXT("ToolDetails", "Arguments & result"))
							]
							.BodyContent()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
								[
									SNew(STextBlock)
										.Text(LOCTEXT("ArgsHdr", "Arguments (JSON)"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(STextBlock)
										.AutoWrapText(true)
										.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
										.Text(FText::FromString(ArgsUi))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.8f, 0.85f, 1.f)))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
								[
									SNew(STextBlock)
										.Text(LOCTEXT("ResHdr", "Result"))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(STextBlock)
										.AutoWrapText(true)
										.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
										.Text(FText::FromString(ResUi.IsEmpty() && bRunning ? FString(TEXT("…")) : ResUi))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.76f, 0.82f, 1.f)))
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
