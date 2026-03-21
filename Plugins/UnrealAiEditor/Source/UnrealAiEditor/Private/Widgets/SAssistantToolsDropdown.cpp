#include "Widgets/SAssistantToolsDropdown.h"

#include "Widgets/SNullWidget.h"
#include "Widgets/UnrealAiToolUi.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SHorizontalBox.h"
#include "Widgets/Layout/SVerticalBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SAssistantToolsDropdown::Construct(const FArguments& InArgs)
{
	TArray<FString> Names = InArgs._ToolNames;
	if (Names.Num() == 0)
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
				.OnGetMenuContent(FOnGetContent::CreateLambda([Names]()
				{
					TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
					for (const FString& Name : Names)
					{
						const EUnrealAiToolVisualCategory Cat = UnrealAiClassifyToolVisuals(Name);
						const FLinearColor DotColor = UnrealAiToolCategoryTint(Cat);
						Box->AddSlot().AutoHeight().Padding(2.f, 1.f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
								[
									SNew(STextBlock)
										.Text(FText::FromString(TEXT("\u2022")))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
										.ColorAndOpacity(FSlateColor(DotColor))
								]
								+ SHorizontalBox::Slot().FillWidth(1.f)
								[
									SNew(STextBlock)
										.Text(FText::FromString(Name))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.95f, 1.f)))
								]
							];
					}
					return SNew(SBorder)
						.BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.14f, 0.98f))
						.Padding(FMargin(8.f))
						[
							Box
						];
				}))
				.ButtonContent()
				[
					SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.72f, 0.8f, 1.f)))
						.Text(FText::Format(LOCTEXT("ToolsCountFmt", "Tools ({0})"), FText::AsNumber(Names.Num())))
				]
		];
}

#undef LOCTEXT_NAMESPACE
