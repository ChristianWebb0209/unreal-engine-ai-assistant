#include "Widgets/SToolCallCard.h"

#include "Style/UnrealAiEditorStyle.h"
#include "Styling/AppStyle.h"
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
#include "Widgets/SToolEditorNotePanel.h"

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

void SToolCallCard::HandleExpansionChanged(const bool bIsExpanded)
{
	if (OnExpansionChanged.IsBound())
	{
		OnExpansionChanged.Execute(bIsExpanded);
	}
}

void SToolCallCard::Construct(const FArguments& InArgs)
{
	ToolName = InArgs._ToolName;
	ArgsPreview = InArgs._ArgumentsPreview;
	ResultPreview = InArgs._ResultPreview;
	bRunning = InArgs._bRunning;
	bSuccess = InArgs._bSuccess;
	EditorPresentation = InArgs._EditorPresentation;
	OnExpansionChanged = InArgs._OnExpansionChanged;

	const EUnrealAiToolVisualCategory Cat = UnrealAiClassifyToolVisuals(ToolName);
	CategoryTint = UnrealAiToolCategoryTint(Cat);
	const FLinearColor DotColor = CategoryTint;

	ChildSlot
		[SNew(SBorder)
				.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.ToolCallCardOuter")))
				.Padding(FMargin(0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
					[
						SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
							.BorderBackgroundColor_Lambda([this, BaseTint = CategoryTint]() -> FSlateColor
							{
								if (!bRunning)
								{
									return FSlateColor(BaseTint);
								}
								const float T =
									FMath::Abs(FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 3.2f));
								const FLinearColor Dim = BaseTint * FLinearColor(0.55f, 0.55f, 0.55f, 0.85f);
								const FLinearColor Bright = BaseTint * FLinearColor(1.f, 1.f, 1.f, 1.f);
								return FSlateColor(FLinearColor::LerpUsingHSV(Dim, Bright, T));
							})
							.VAlign(VAlign_Fill)
							[
								SNew(SBox).WidthOverride(4.f)[SNew(SSpacer)]
							]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SExpandableArea)
							.InitiallyCollapsed(InArgs._bInitiallyCollapsed)
							.OnAreaExpansionChanged(
								FOnBooleanValueChanged::CreateSP(this, &SToolCallCard::HandleExpansionChanged))
							.AreaTitle(FText::GetEmpty())
							.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
							.BorderBackgroundColor(FUnrealAiEditorStyle::LinearColorToolCallCardInset())
							.HeaderContent()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.f, 0.f, 4.f, 0.f))
							[
								SNew(STextBlock)
									.Text(FText::FromString(TEXT("\u2022")))
									.Font(FUnrealAiEditorStyle::FontLabelBold())
									.ColorAndOpacity(FSlateColor(DotColor))
							]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
									.Text(FText::FromString(ToolName))
									.Font(FUnrealAiEditorStyle::FontListRowTitle())
									.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextPrimary())
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
									.Font(FUnrealAiEditorStyle::FontCaption())
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
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(3.f, 1.f, 3.f, 1.f))
							[
								SNew(STextBlock)
									.Text(LOCTEXT("ArgsHdr", "Arguments (JSON)"))
									.Font(FUnrealAiEditorStyle::FontPopoverTitle())
									.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMetaHint())
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 0.f, 4.f, 4.f))
							[
								SNew(SBorder)
									.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.ToolCallCodeWell")))
									.Padding(FMargin(6.f, 5.f))
									[
										SNew(SBox)
											.MinDesiredWidth(200.f)
											.MaxDesiredHeight(160.f)
											[
												SNew(SMultiLineEditableText)
													.IsReadOnly(true)
													.AutoWrapText(true)
													.Font(FUnrealAiEditorStyle::FontMono8())
													.Text(FText::FromString(ArgsPreview.IsEmpty() ? TEXT("(empty)") : ArgsPreview))
											]
									]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 0.f, 4.f, 2.f))
							[
								SNew(STextBlock)
									.Text(LOCTEXT("ResHdr", "Result"))
									.Font(FUnrealAiEditorStyle::FontPopoverTitle())
									.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMetaHint())
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 0.f, 4.f, 2.f))
							[
								SNew(SBorder)
									.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.ToolCallCodeWell")))
									.Padding(FMargin(6.f, 5.f))
									[
										SNew(SBox)
											.MinDesiredWidth(200.f)
											.MaxDesiredHeight(200.f)
											[
												SNew(SMultiLineEditableText)
													.IsReadOnly(true)
													.AutoWrapText(true)
													.Font(FUnrealAiEditorStyle::FontMono8())
													.Text(FText::FromString(
														ResultPreview.IsEmpty() && bRunning ? FString(TEXT("…")) : ResultPreview))
											]
									]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f, 0.f, 0.f))
							[
								SNew(SToolEditorNotePanel)
									.EditorPresentation(EditorPresentation)
							]
						]
					]
				]
		];

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
