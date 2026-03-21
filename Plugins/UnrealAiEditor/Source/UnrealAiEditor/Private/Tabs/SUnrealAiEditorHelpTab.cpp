#include "Tabs/SUnrealAiEditorHelpTab.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SUnrealAiEditorHelpTab::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SBorder)
				.Padding(FMargin(12.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						SNew(STextBlock)
							.Text(LOCTEXT("HelpTitle", "Help"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
					[
						SNew(STextBlock)
							.AutoWrapText(true)
							.Text(LOCTEXT(
								"HelpBody",
								"Documentation lives under the project docs/ folder (PRD, context service, chat renderer). "
								"Plugin README: Plugins/UnrealAiEditor/README.md."))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
						[
							SNew(SButton)
								.Text(LOCTEXT("OpenDocs", "Open docs folder"))
								.OnClicked(this, &SUnrealAiEditorHelpTab::OnOpenDocsFolder)
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
						[
							SNew(SButton)
								.Text(LOCTEXT("OpenLogs", "Open Saved/Logs"))
								.OnClicked(this, &SUnrealAiEditorHelpTab::OnOpenLogsFolder)
						]
					]
				]
		];
}

FReply SUnrealAiEditorHelpTab::OnOpenDocsFolder()
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("docs"));
	FPlatformProcess::ExploreFolder(*Path);
	return FReply::Handled();
}

FReply SUnrealAiEditorHelpTab::OnOpenLogsFolder()
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Logs"));
	FPlatformProcess::ExploreFolder(*Path);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
