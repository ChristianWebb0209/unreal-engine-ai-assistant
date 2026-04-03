#include "Tabs/SUnrealAiEditorChatTab.h"

#include "Async/Async.h"
#include "UnrealAiEditorModule.h"
#include "Style/UnrealAiEditorStyle.h"
#include "UnrealAiEditorTabIds.h"
#include "Context/UnrealAiContextDragDrop.h"
#include "Context/UnrealAiProjectId.h"
#include "Context/UnrealAiStartupOpsStatus.h"
#include "Retrieval/IUnrealAiRetrievalService.h"
#include "Retrieval/UnrealAiRetrievalTypes.h"
#include "Widgets/SChatComposer.h"
#include "Widgets/UnrealAiChatUiHelpers.h"
#include "Widgets/SChatHeader.h"
#include "Widgets/SChatMessageList.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDesktopPlatform.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Internationalization/Text.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace
{
	/** Bottom-of-chat status: phased indexing progress, chunk + wave counts, ETA for full embed, partial-DB hint. */
	class SIndexingProjectFooterLabel final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SIndexingProjectFooterLabel) {}
		SLATE_ARGUMENT(TWeakPtr<FUnrealAiBackendRegistry>, BackendRegistry)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			BackendRegistryWeak = InArgs._BackendRegistry;
			ChildSlot
				[
					SAssignNew(Label, STextBlock)
						.Font(FUnrealAiEditorStyle::FontCaption())
						.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
						.Visibility(EVisibility::Collapsed)
				];
			RegisterActiveTimer(
				0.35f,
				FWidgetActiveTimerDelegate::CreateSP(this, &SIndexingProjectFooterLabel::TickAnimate));
		}

	private:
		EActiveTimerReturnType TickAnimate(double /*CurrentTime*/, float /*DeltaTime*/)
		{
			if (!Label.IsValid())
			{
				return EActiveTimerReturnType::Continue;
			}
			const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
			bool bIndexing = false;
			FUnrealAiRetrievalProjectStatus LastStatus;
			if (const TSharedPtr<FUnrealAiBackendRegistry> Reg = BackendRegistryWeak.Pin())
			{
				if (IUnrealAiRetrievalService* Retrieval = Reg->GetRetrievalService())
				{
					if (Retrieval->IsEnabledForProject(ProjectId))
					{
						LastStatus = Retrieval->GetProjectStatus(ProjectId);
						bIndexing = LastStatus.bBusy
							|| LastStatus.StateText.Equals(TEXT("indexing"), ESearchCase::IgnoreCase);
					}
				}
			}
			if (!bIndexing)
			{
				Label->SetVisibility(EVisibility::Collapsed);
				return EActiveTimerReturnType::Continue;
			}
			Label->SetVisibility(EVisibility::Visible);
			static const TCHAR* const DotSuffix[] = { TEXT(""), TEXT("."), TEXT(".."), TEXT("...") };
			DotPhase = (DotPhase + 1) % 4;
			const FText DotsText = FText::FromString(FString(DotSuffix[DotPhase]));

			FText EtaFullIndex = FText::GetEmpty();
			const int32 TargetN = LastStatus.IndexBuildTargetChunks;
			const int32 DoneN = LastStatus.IndexBuildCompletedChunks;
			const int32 WaveTotal = LastStatus.IndexBuildWaveTotal;
			const int32 WaveDone = LastStatus.IndexBuildWaveDone;
			const int32 DbChunks = LastStatus.ChunksIndexed;
			if (TargetN > 0)
			{
				if (DoneN <= 0)
				{
					EtaFullIndex = LOCTEXT("IndexingEtaPending", "ETA: not enough progress yet — wait for first embeddings");
				}
				else if (DoneN < TargetN && LastStatus.IndexBuildPhaseStartedUtc != FDateTime::MinValue())
				{
					const double ElapsedSec = (FDateTime::UtcNow() - LastStatus.IndexBuildPhaseStartedUtc).GetTotalSeconds();
					if (ElapsedSec > 0.25)
					{
						const double Rate = static_cast<double>(DoneN) / ElapsedSec;
						if (Rate > 1.e-9)
						{
							const int32 RemainingTotalSec = FMath::Clamp(
								static_cast<int32>(FMath::RoundToDouble(static_cast<double>(TargetN - DoneN) / Rate)),
								0,
								24 * 3600);
							const int32 EtaMin = RemainingTotalSec / 60;
							const int32 EtaSec = RemainingTotalSec % 60;
							EtaFullIndex = FText::Format(
								LOCTEXT("IndexingEtaFullIndexFmt", "~{0}m {1}s to finish embedding all chunks (rough estimate)"),
								FText::AsNumber(EtaMin),
								FText::AsNumber(EtaSec));
						}
					}
				}
			}

			TArray<FText> Parts;
			Parts.Reserve(6);
			Parts.Add(FText::Format(LOCTEXT("IndexingProjectLeadFmt", "Indexing project{0}"), DotsText));
			if (TargetN > 0)
			{
				Parts.Add(FText::Format(
					LOCTEXT("IndexingChunksProgressFmt", "{0}/{1} chunks embedded"),
					FText::AsNumber(DoneN),
					FText::AsNumber(TargetN)));
			}
			else
			{
				Parts.Add(LOCTEXT("IndexingPreparingCorpus", "preparing corpus…"));
			}
			if (WaveTotal > 0)
			{
				Parts.Add(FText::Format(
					LOCTEXT("IndexingWavesFmt", "priority waves {0}/{1} written to disk"),
					FText::AsNumber(WaveDone),
					FText::AsNumber(WaveTotal)));
			}
			if (DbChunks > 0)
			{
				Parts.Add(FText::Format(
					LOCTEXT("IndexingPartialDbFmt", "{0} chunks in DB — search may already return results (coverage grows)"),
					FText::AsNumber(DbChunks)));
			}
			if (!EtaFullIndex.IsEmpty())
			{
				Parts.Add(EtaFullIndex);
			}
			Parts.Add(LOCTEXT(
				"IndexingPhasedExplain",
				"High-priority paths are indexed first; long ETA is for the entire job, not a lockout."));
			const FText Line = FText::Join(FText::FromString(TEXT(" · ")), Parts);
			Label->SetText(Line);
			Label->SetToolTipText(Line);
			return EActiveTimerReturnType::Continue;
		}

		TWeakPtr<FUnrealAiBackendRegistry> BackendRegistryWeak;
		TSharedPtr<STextBlock> Label;
		int32 DotPhase = 0;
	};

	/** Single-row startup / discovery / retrieval status (three captions on one line). */
	class SStartupOpsFooterStrip final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SStartupOpsFooterStrip) {}
		SLATE_ARGUMENT(TWeakPtr<FUnrealAiBackendRegistry>, BackendRegistry)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			BackendRegistryWeak = InArgs._BackendRegistry;
			ChildSlot
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SAssignNew(TextAggregate, STextBlock)
							.Font(FUnrealAiEditorStyle::FontCaption())
							.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(14.f, 0.f, 0.f, 0.f))
					[
						SAssignNew(TextDiscovery, STextBlock)
							.Font(FUnrealAiEditorStyle::FontCaption())
							.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(14.f, 0.f, 0.f, 0.f))
					[
						SAssignNew(TextRetrieval, STextBlock)
							.Font(FUnrealAiEditorStyle::FontCaption())
							.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SSpacer)
					]
				];
			RegisterActiveTimer(
				0.35f,
				FWidgetActiveTimerDelegate::CreateSP(this, &SStartupOpsFooterStrip::TickRefresh));
			SyncFooterTexts();
		}

	private:
		void SyncFooterTexts()
		{
			if (!TextAggregate.IsValid() || !TextDiscovery.IsValid() || !TextRetrieval.IsValid())
			{
				return;
			}
			UnrealAiStartupOpsStatus::FFooterStrip Strip;
			const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
			if (const TSharedPtr<FUnrealAiBackendRegistry> Reg = BackendRegistryWeak.Pin())
			{
				UnrealAiStartupOpsStatus::BuildFooterStrip(Reg, ProjectId, Strip);
			}
			else
			{
				Strip.Aggregate = TEXT("Startup ops: —");
				Strip.Discovery = TEXT("discovery=—");
				Strip.Retrieval = TEXT("retrieval=—");
			}
			TextAggregate->SetText(FText::FromString(Strip.Aggregate));
			TextDiscovery->SetText(FText::FromString(Strip.Discovery));
			TextRetrieval->SetText(FText::FromString(Strip.Retrieval));
		}

		EActiveTimerReturnType TickRefresh(double /*CurrentTime*/, float /*DeltaTime*/)
		{
			SyncFooterTexts();
			return EActiveTimerReturnType::Continue;
		}

		TWeakPtr<FUnrealAiBackendRegistry> BackendRegistryWeak;
		TSharedPtr<STextBlock> TextAggregate;
		TSharedPtr<STextBlock> TextDiscovery;
		TSharedPtr<STextBlock> TextRetrieval;
	};
}

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
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(6.f, 2.f, 6.f, 0.f))
			[
				SNew(SIndexingProjectFooterLabel)
					.BackendRegistry(BackendRegistry)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(6.f, 2.f, 6.f, 4.f))
			[
				SNew(SStartupOpsFooterStrip)
					.BackendRegistry(BackendRegistry)
			]
		];

	if (InArgs._bUseExplicitThreadId && InArgs._ExplicitThreadId.IsValid())
	{
		UnrealAiChatUi_LoadPersistedThreadIntoUi(BackendRegistry, Session, MessageListWidget);
	}
	UnrealAiChatUi_MaybeInjectLlmSetupNotice(BackendRegistry, MessageListWidget);

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
