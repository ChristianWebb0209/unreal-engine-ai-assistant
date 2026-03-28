#include "Tabs/SUnrealAiEditorChatTab.h"

#include "Async/Async.h"
#include "UnrealAiEditorModule.h"
#include "Style/UnrealAiEditorStyle.h"
#include "UnrealAiEditorTabIds.h"
#include "Context/UnrealAiContextDragDrop.h"
#include "Context/UnrealAiProjectId.h"
#include "Retrieval/IUnrealAiRetrievalService.h"
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
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace UnrealAiChatTabChrome
{
	static TSharedPtr<SDockTab> FindParentDockTab(const TSharedRef<const SWidget>& Widget)
	{
		TSharedPtr<SWidget> Current = Widget->GetParentWidget();
		while (Current.IsValid())
		{
			if (Current->GetType() == FName(TEXT("SDockTab")))
			{
				return StaticCastSharedPtr<SDockTab>(Current);
			}
			Current = Current->GetParentWidget();
		}
		return nullptr;
	}
}

void SUnrealAiEditorChatTab::SetHostDockTab(const TSharedRef<SDockTab>& InHostTab)
{
	HostDockTab = InHostTab;
	RefreshChatChrome();
}

void SUnrealAiEditorChatTab::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;
	Session = MakeShared<FUnrealAiChatUiSession>();
	Session->OnChatNameChanged.AddSP(this, &SUnrealAiEditorChatTab::RefreshChatChrome);
	FUnrealAiEditorModule::SetActiveChatSession(Session);
	if (BackendRegistry.IsValid())
	{
		if (FUnrealAiModelProfileRegistry* Reg = BackendRegistry->GetModelProfileRegistry())
		{
			Session->ModelProfileId = Reg->GetDefaultModelId();
		}
		if (InArgs._bUseExplicitThreadId && InArgs._ExplicitThreadId.IsValid())
		{
			Session->ThreadId = InArgs._ExplicitThreadId;
		}
		else
		{
			Session->ThreadId = FGuid::NewGuid();
		}
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
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 3.f))
			[
				SNew(STextBlock)
					.Font(FUnrealAiEditorStyle::FontCaption())
					.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
					.Text_Lambda([this]()
					{
						if (!BackendRegistry.IsValid() || !BackendRegistry->GetRetrievalService())
						{
							return FText::FromString(TEXT("Retrieval: unavailable"));
						}
						const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
						const FUnrealAiRetrievalProjectStatus Status = BackendRegistry->GetRetrievalService()->GetProjectStatus(ProjectId);
						if (!Status.bEnabled)
						{
							return FText::FromString(TEXT("Retrieval: disabled"));
						}
						const FString BusySuffix = Status.bBusy ? TEXT(" (updating...)") : FString();
						return FText::FromString(FString::Printf(
							TEXT("Retrieval: %s%s | files=%d chunks=%d"),
							*Status.StateText,
							*BusySuffix,
							Status.FilesIndexed,
							Status.ChunksIndexed));
					})
			]
		];

	if (InArgs._bUseExplicitThreadId && InArgs._ExplicitThreadId.IsValid())
	{
		UnrealAiChatUi_LoadPersistedThreadIntoUi(BackendRegistry, Session, MessageListWidget);
	}

	RefreshChatChrome();
	const TWeakPtr<SUnrealAiEditorChatTab> WeakChatForChrome = SharedThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakChatForChrome]()
	{
		if (const TSharedPtr<SUnrealAiEditorChatTab> Self = WeakChatForChrome.Pin())
		{
			Self->RefreshChatChrome();
		}
	});
}

SUnrealAiEditorChatTab::~SUnrealAiEditorChatTab()
{
	FUnrealAiEditorModule::UnregisterAgentChatTabForPersistence(this);
	if (Session.IsValid())
	{
		Session->OnChatNameChanged.RemoveAll(this);
	}
	if (FUnrealAiEditorModule::GetActiveChatSession() == Session)
	{
		FUnrealAiEditorModule::SetActiveChatSession(nullptr);
	}
}

void SUnrealAiEditorChatTab::RefreshChatChrome()
{
	const FText DockLabel = (Session.IsValid() && !Session->ChatName.IsEmpty())
		? FText::FromString(Session->ChatName)
		: LOCTEXT("AgentChatDockFallback", "Agent Chat");
	TSharedPtr<SDockTab> DockTab = HostDockTab.Pin();
	if (!DockTab.IsValid())
	{
		DockTab = UnrealAiChatTabChrome::FindParentDockTab(StaticCastSharedRef<const SWidget>(AsShared()));
	}
	if (DockTab.IsValid())
	{
		DockTab->SetLabel(DockLabel);
	}
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
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
