#include "Tabs/SUnrealAiEditorChatTab.h"

#include "UnrealAiEditorTabIds.h"
#include "Widgets/SChatComposer.h"
#include "Widgets/SChatHeader.h"
#include "Widgets/SChatMessageList.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiProjectId.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SVerticalBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SHorizontalBox.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SUnrealAiEditorChatTab::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;
	Session = MakeShared<FUnrealAiChatUiSession>();
	if (BackendRegistry.IsValid())
	{
		if (FUnrealAiModelProfileRegistry* Reg = BackendRegistry->GetModelProfileRegistry())
		{
			Session->ModelProfileId = Reg->GetDefaultModelId();
		}
		Session->ThreadId = FGuid::NewGuid();
	}

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SChatHeader)
					.BackendRegistry(BackendRegistry)
					.Session(Session)
					.OnOpenSettings(FSimpleDelegate::CreateSP(this, &SUnrealAiEditorChatTab::OpenSettingsTab))
					.OnNewChat(FSimpleDelegate::CreateSP(this, &SUnrealAiEditorChatTab::OnUnifiedNewChat))
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(MessageListWidget, SChatMessageList)
					.BackendRegistry(BackendRegistry)
					.Session(Session)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder)
					.BorderBackgroundColor(FLinearColor(0.16f, 0.14f, 0.1f, 0.85f))
					.Padding(FMargin(8.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Text(LOCTEXT(
									"PermHint",
									"Destructive tools: permission prompts (Allow once / Always / Deny) will appear here when policy is enforced."))
								.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.62f, 0.55f, 1.f)))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
						[
							SNew(SButton)
								.IsEnabled(false)
								.Text(LOCTEXT("AllowOnce", "Allow once"))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
						[
							SNew(SButton)
								.IsEnabled(false)
								.Text(LOCTEXT("Deny", "Deny"))
						]
					]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(ComposerWidget, SChatComposer)
					.BackendRegistry(BackendRegistry)
					.MessageList(MessageListWidget)
					.Session(Session)
					.OnNewChat(FSimpleDelegate::CreateSP(this, &SUnrealAiEditorChatTab::OnUnifiedNewChat))
			]
		];
}

void SUnrealAiEditorChatTab::OnUnifiedNewChat()
{
	if (!BackendRegistry.IsValid() || !Session.IsValid())
	{
		return;
	}
	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString OldThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->SaveNow(ProjectId, OldThreadIdStr);
	}
	Session->ThreadId = FGuid::NewGuid();
	const FString NewThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->LoadOrCreate(ProjectId, NewThreadIdStr);
	}
	if (MessageListWidget.IsValid())
	{
		MessageListWidget->ClearTranscript();
	}
	if (ComposerWidget.IsValid())
	{
		ComposerWidget->SyncAttachmentChipsUi();
	}
}

void SUnrealAiEditorChatTab::OpenSettingsTab() const
{
	FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::SettingsTab);
}

#undef LOCTEXT_NAMESPACE
