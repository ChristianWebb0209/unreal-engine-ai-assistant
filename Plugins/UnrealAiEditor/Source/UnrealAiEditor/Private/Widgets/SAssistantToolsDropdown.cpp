#include "Widgets/SAssistantToolsDropdown.h"

#include "Misc/Guid.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/UnrealAiToolUi.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace UnrealAiToolsDropdownUi
{
	static FText StatusLine(const FUnrealAiAssistantSegmentToolInfo& T)
	{
		if (T.bRunning)
		{
			return LOCTEXT("ToolRun", "Running…");
		}
		return T.bOk ? LOCTEXT("ToolOk", "Completed") : LOCTEXT("ToolFail", "Failed");
	}

	static FSlateColor StatusColor(const FUnrealAiAssistantSegmentToolInfo& T)
	{
		if (T.bRunning)
		{
			return FSlateColor(FLinearColor(0.85f, 0.75f, 0.35f, 1.f));
		}
		return T.bOk ? FSlateColor(FLinearColor(0.45f, 0.85f, 0.55f, 1.f))
					 : FSlateColor(FLinearColor(0.95f, 0.45f, 0.45f, 1.f));
	}
}

void SAssistantToolsDropdown::Construct(const FArguments& InArgs)
{
	TArray<FUnrealAiAssistantSegmentToolInfo> Details = InArgs._ToolDetails;
	const FGuid RunId = InArgs._RunId;

	if (Details.Num() == 0)
	{
		ChildSlot
			[
				SNullWidget::NullWidget
			];
		return;
	}

	ChildSlot
		[
			SNew(SComboButton)
				.ContentPadding(FMargin(6.f, 2.f))
				.ToolTipText(LOCTEXT("ToolsVerboseTip", "Open detailed tool log (arguments, results, call ids)"))
				.OnGetMenuContent(FOnGetContent::CreateLambda([Details, RunId]()
				{
					TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);
					if (RunId.IsValid())
					{
						Root->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
							[
								SNew(STextBlock)
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.58f, 0.65f, 1.f)))
									.Text(FText::Format(
										LOCTEXT("RunIdFmt", "Run id: {0}"),
										FText::FromString(RunId.ToString(EGuidFormats::DigitsWithHyphens))))
							];
					}

					for (int32 Idx = 0; Idx < Details.Num(); ++Idx)
					{
						const FUnrealAiAssistantSegmentToolInfo& T = Details[Idx];
						const EUnrealAiToolVisualCategory Cat = UnrealAiClassifyToolVisuals(T.ToolName);
						const FLinearColor DotColor = UnrealAiToolCategoryTint(Cat);

						TSharedRef<SVerticalBox> Block = SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
								[
									SNew(STextBlock)
										.Text(FText::FromString(TEXT("\u2022")))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
										.ColorAndOpacity(FSlateColor(DotColor))
								]
								+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
								[
									SNew(STextBlock)
										.Text(FText::FromString(T.ToolName.IsEmpty() ? TEXT("(tool)") : T.ToolName))
										.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.96f, 1.f, 1.f)))
								]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(8.f, 0.f, 0.f, 0.f))
								[
									SNew(STextBlock)
										.Text(UnrealAiToolsDropdownUi::StatusLine(T))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
										.ColorAndOpacity(UnrealAiToolsDropdownUi::StatusColor(T))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
							[
								SNew(STextBlock)
									.Text(FText::Format(
										LOCTEXT("CallIdFmt", "Call id: {0}"),
										FText::FromString(T.ToolCallId.IsEmpty() ? TEXT("(none)") : T.ToolCallId)))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.58f, 0.62f, 1.f)))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 2.f))
							[
								SNew(STextBlock)
									.Text(LOCTEXT("ArgsHdr", "Arguments (JSON)"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.75f, 0.85f, 1.f)))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
							[
								SNew(SBox)
									.MinDesiredWidth(420.f)
									.MinDesiredHeight(48.f)
									.MaxDesiredHeight(180.f)
									[
										SNew(SMultiLineEditableText)
											.IsReadOnly(true)
											.AutoWrapText(true)
											.Text(FText::FromString(
												T.ArgsPreview.IsEmpty() ? TEXT("(empty)") : T.ArgsPreview))
									]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 2.f))
							[
								SNew(STextBlock)
									.Text(LOCTEXT("ResHdr", "Result"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.75f, 0.85f, 1.f)))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
							[
								SNew(SBox)
									.MinDesiredWidth(420.f)
									.MinDesiredHeight(48.f)
									.MaxDesiredHeight(220.f)
									[
										SNew(SMultiLineEditableText)
											.IsReadOnly(true)
											.AutoWrapText(true)
											.Text(FText::FromString(
												T.ResultPreview.IsEmpty() ? TEXT("(empty)") : T.ResultPreview))
									]
							];

						Root->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
							[
								SNew(SBorder)
									.BorderBackgroundColor(FLinearColor(0.1f, 0.11f, 0.13f, 1.f))
									.Padding(FMargin(10.f, 8.f))
									[
										Block
									]
							];

						if (Idx < Details.Num() - 1)
						{
							Root->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
								[
									SNew(SBorder)
										.BorderBackgroundColor(FLinearColor(0.25f, 0.26f, 0.3f, 0.5f))
										[
											SNew(SBox)
												.HeightOverride(1.f)
												[
													SNew(SSpacer)
												]
										]
								];
						}
					}

					return SNew(SBorder)
						.BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.14f, 0.98f))
						.Padding(FMargin(10.f, 8.f))
						[
							SNew(SBox)
								.MinDesiredWidth(460.f)
								.MaxDesiredHeight(520.f)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										Root
									]
								]
						];
				}))
				.ButtonContent()
				[
					SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.72f, 0.8f, 1.f)))
						.Text(FText::Format(LOCTEXT("ToolsCountFmt", "Tools ({0})"), FText::AsNumber(Details.Num())))
				]
		];
}

#undef LOCTEXT_NAMESPACE
