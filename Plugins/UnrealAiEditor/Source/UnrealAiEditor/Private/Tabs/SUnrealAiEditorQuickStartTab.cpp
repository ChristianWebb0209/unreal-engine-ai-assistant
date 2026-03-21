#include "Tabs/SUnrealAiEditorQuickStartTab.h"

#include "UnrealAiEditorTabIds.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SUnrealAiEditorQuickStartTab::Construct(const FArguments& InArgs)
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
							.Text(LOCTEXT("QsTitle", "Quick Start"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
					[
						SNew(STextBlock)
							.AutoWrapText(true)
							.Text(LOCTEXT(
								"QsBody",
								"1) Window → Unreal AI → Agent Chat (or Tools → Unreal AI)\n"
								"2) Add an API key in AI Settings (plugin_settings.json) — otherwise the LLM runs in stub mode\n"
								"3) Drag assets or actors into Agent Chat, or right-click → Unreal AI → Add to context; @ mentions resolve assets\n"
								"4) Send a prompt; use Stop to cancel streaming or tool execution\n"
								"5) New Chat saves the current thread context and starts a fresh thread"))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(8.f)
					[
						SNew(SButton)
							.Text(LOCTEXT("OpenChat", "Open Agent Chat"))
							.OnClicked(this, &SUnrealAiEditorQuickStartTab::OnOpenChat)
					]
				]
		];
}

FReply SUnrealAiEditorQuickStartTab::OnOpenChat()
{
	FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ChatTab);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
