#include "Tabs/SUnrealAiEditorChatTab.h"

#include "UnrealAiEditorModule.h"
#include "UnrealAiEditorTabIds.h"
#include "Context/UnrealAiContextDragDrop.h"
#include "Widgets/SChatComposer.h"
#include "Widgets/SChatHeader.h"
#include "Widgets/SChatMessageList.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Widgets/UnrealAiChatUiHelpers.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SUnrealAiEditorChatTab::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;
	Session = MakeShared<FUnrealAiChatUiSession>();
	FUnrealAiEditorModule::SetActiveChatSession(Session);
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
				SAssignNew(ComposerWidget, SChatComposer)
					.BackendRegistry(BackendRegistry)
					.MessageList(MessageListWidget)
					.Session(Session)
			]
		];
}

SUnrealAiEditorChatTab::~SUnrealAiEditorChatTab()
{
	if (FUnrealAiEditorModule::GetActiveChatSession() == Session)
	{
		FUnrealAiEditorModule::SetActiveChatSession(nullptr);
	}
}

FReply SUnrealAiEditorChatTab::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TArray<FContextAttachment> Tmp;
	if (UnrealAiContextDragDrop::TryParseDragDrop(DragDropEvent, Tmp))
	{
		return FReply::Handled();
	}
	return SCompoundWidget::OnDragOver(MyGeometry, DragDropEvent);
}

FReply SUnrealAiEditorChatTab::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TArray<FContextAttachment> Atts;
	if (!UnrealAiContextDragDrop::TryParseDragDrop(DragDropEvent, Atts))
	{
		return FReply::Unhandled();
	}
	UnrealAiContextDragDrop::AddAttachmentsToActiveChat(BackendRegistry, Session, Atts);
	if (ComposerWidget.IsValid())
	{
		ComposerWidget->SyncAttachmentChipsUi();
	}
	FUnrealAiEditorModule::NotifyContextAttachmentsChanged();
	return FReply::Handled();
}

void SUnrealAiEditorChatTab::OnUnifiedNewChat()
{
	FUnrealAiEditorModule::OpenNewAgentChatTabBeside(AsShared());
}

void SUnrealAiEditorChatTab::OpenSettingsTab() const
{
	FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::SettingsTab);
}

void SUnrealAiEditorChatTab::MenuNewChat()
{
	OnUnifiedNewChat();
}

void SUnrealAiEditorChatTab::MenuExportChat()
{
	if (!MessageListWidget.IsValid())
	{
		return;
	}
	const TSharedPtr<FUnrealAiChatTranscript> T = MessageListWidget->GetTranscript();
	if (!T.IsValid())
	{
		return;
	}
	const FString Text = T->FormatPlainText();
	if (Text.IsEmpty())
	{
		return;
	}
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return;
	}
	const FString DefaultName = FString::Printf(
		TEXT("agent-chat-%s.txt"),
		*Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens));
	TArray<FString> SaveFilenames;
	const bool bOk = DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
		TEXT("Export chat"),
		FPaths::ProjectSavedDir(),
		*DefaultName,
		TEXT("Text files (*.txt)|*.txt|All files (*.*)|*.*"),
		EFileDialogFlags::None,
		SaveFilenames);
	if (bOk && SaveFilenames.Num() > 0)
	{
		FFileHelper::SaveStringToFile(Text, *SaveFilenames[0]);
	}
}

void SUnrealAiEditorChatTab::MenuCopyChatToClipboard()
{
	if (!MessageListWidget.IsValid())
	{
		return;
	}
	const TSharedPtr<FUnrealAiChatTranscript> T = MessageListWidget->GetTranscript();
	if (!T.IsValid())
	{
		return;
	}
	const FString Text = T->FormatPlainText();
	if (!Text.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*Text);
	}
}

void SUnrealAiEditorChatTab::MenuDeleteChat()
{
	UnrealAiChatUi_DeleteChatPermanently(BackendRegistry, Session, MessageListWidget);
	if (ComposerWidget.IsValid())
	{
		ComposerWidget->ResetComposerInput();
		ComposerWidget->SyncAttachmentChipsUi();
	}
}

#undef LOCTEXT_NAMESPACE
