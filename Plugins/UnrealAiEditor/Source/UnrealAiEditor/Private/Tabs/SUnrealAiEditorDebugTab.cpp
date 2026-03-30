#include "Tabs/SUnrealAiEditorDebugTab.h"

#include "UnrealAiEditorModule.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiProjectId.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/MessageDialog.h"
#include "Misc/FileHelper.h"
#include "Serialization/Archive.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SUnrealAiLocalJsonInspectorPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

static constexpr int32 GMaxDirDepth = 8;
static constexpr float GAutoRefreshSeconds = 3.f;

static FString UnrealAiNormalizeDebugPath(FString Path)
{
	FPaths::NormalizeFilename(Path);
	return FPaths::ConvertRelativePathToFull(Path);
}

void SUnrealAiEditorDebugTab::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;

	IUnrealAiPersistence* Persist =
		BackendRegistry.IsValid() ? BackendRegistry->GetPersistence() : nullptr;
	const FString Root = Persist ? Persist->GetDataRootDirectory() : FString();
	DataRootShort = FText::FromString(Root.IsEmpty() ? TEXT("(no persistence)") : FPaths::GetCleanFilename(Root));
	DataRootTooltip = FText::FromString(Root);

	const FString ProjId = UnrealAiProjectId::GetCurrentProjectId();
	ProjectIdLabel = FText::Format(LOCTEXT("DbgProjectFmt", "project_id: {0}"), FText::FromString(ProjId));

	RebuildFileList();
	RefreshActiveSessionUi();

	TSharedPtr<SUnrealAiEditorDebugTab> Self = SharedThis(this);

	ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.BorderSubtle")))
			.Padding(8.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(SButton)
					.ButtonColorAndOpacity(FLinearColor(0.55f, 0.12f, 0.12f, 1.f))
					.ForegroundColor(FLinearColor::White)
					.ContentPadding(FMargin(10.f, 6.f))
					.Text(LOCTEXT("DbgDeleteAll", "Delete all local chat data"))
					.ToolTipText(LOCTEXT(
						"DbgDeleteAllTip",
						"Removes all chats under the local data root and Project Saved/UnrealAiEditor. "
						"Keeps settings (plugin_settings.json, usage_stats.json, model profiles, API keys)."))
					.OnClicked_Lambda([Self]()
					{
						return Self->OnDeleteAllLocalChatDataClicked();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DbgTitle", "Unreal AI — Debug"))
						.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
						.ColorAndOpacity(FUnrealAiEditorStyle::ColorAccent())
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("DbgRefresh", "Refresh"))
							.OnClicked_Lambda([Self]()
							{
								Self->OnRefreshClicked();
								return FReply::Handled();
							})
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
						[
							SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
							.IsChecked(Self.Get(), &SUnrealAiEditorDebugTab::GetAutoRefreshCheckState)
							.OnCheckStateChanged_Lambda([Self](ECheckBoxState S)
							{
								Self->OnAutoRefreshToggled(S);
							})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("DbgAuto", "Auto-refresh (3s)"))
								.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextPrimary())
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DbgDataRoot", "Data root"))
							.ColorAndOpacity(FUnrealAiEditorStyle::ColorDebugMuted())
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
						[
							SNew(STextBlock)
							.Text_Lambda([Self]() { return Self->DataRootShort; })
							.ToolTipText_Lambda([Self]() { return Self->DataRootTooltip; })
							.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextPrimary())
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(STextBlock)
						.Text_Lambda([Self]() { return Self->ProjectIdLabel; })
						.Font(FUnrealAiEditorStyle::FontMono9())
						.ColorAndOpacity(FUnrealAiEditorStyle::ColorDebugMuted())
					]
				]

				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SSplitter)
					.Orientation(Orient_Horizontal)
					+ SSplitter::Slot().Value(0.38f)
					[
						SNew(SBorder)
						.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.DebugNav")))
						.Padding(6.f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("DbgActiveHdr", "Active Agent Chat"))
									.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
									.ColorAndOpacity(FUnrealAiEditorStyle::ColorAccent())
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
								[
									SNew(STextBlock)
									.Text_Lambda([Self]() { return Self->SummaryLine; })
									.WrapTextAt(260.f)
									.AutoWrapText(true)
									.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextPrimary())
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
									[
										SNew(SButton)
										.Text(LOCTEXT("DbgCtx", "Context JSON"))
										.OnClicked_Lambda([Self]()
										{
											Self->OnLoadActiveContext();
											return FReply::Handled();
										})
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(SButton)
										.Text(LOCTEXT("DbgConv", "Conversation JSON"))
										.OnClicked_Lambda([Self]()
										{
											Self->OnLoadActiveConversation();
											return FReply::Handled();
										})
									]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("DbgNavHdr", "Local files"))
									.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
									.ColorAndOpacity(FUnrealAiEditorStyle::ColorAccent())
								]
								+ SVerticalBox::Slot().FillHeight(1.f)
								[
									SAssignNew(FileList, SListView<TSharedPtr<FUnrealAiDebugListEntry>>)
									.ListItemsSource(&FileEntries)
									.SelectionMode(ESelectionMode::Single)
									.OnGenerateRow(Self.Get(), &SUnrealAiEditorDebugTab::OnGenerateRow)
									.OnSelectionChanged(Self.Get(), &SUnrealAiEditorDebugTab::OnSelectionChanged)
								]
							]
						]
					]
					+ SSplitter::Slot().Value(0.62f)
					[
						SNew(SBorder)
						.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.DebugInspect")))
						.Padding(6.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("DbgInspectHdr", "Inspector"))
									.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
									.ColorAndOpacity(FUnrealAiEditorStyle::ColorAccent())
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SButton)
									.Text(LOCTEXT("DbgCopy", "Copy"))
									.OnClicked_Lambda([Self]()
									{
										Self->OnCopyInspectorClicked();
										return FReply::Handled();
									})
								]
							]
							+ SVerticalBox::Slot().FillHeight(1.f)
							[
								SAssignNew(InspectorPanel, SUnrealAiLocalJsonInspectorPanel)
							]
						]
					]
				]
			]
		];

	if (InspectorPanel.IsValid())
	{
		InspectorPanel->SetInspectorText(LOCTEXT("DbgPick", "Select a file from the list.").ToString());
	}

	if (bAutoRefresh)
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(this, &SUnrealAiEditorDebugTab::OnAutoRefreshTick));
	}
}

SUnrealAiEditorDebugTab::~SUnrealAiEditorDebugTab()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

void SUnrealAiEditorDebugTab::RebuildFileList()
{
	FileEntries.Reset();

	IUnrealAiPersistence* Persist =
		BackendRegistry.IsValid() ? BackendRegistry->GetPersistence() : nullptr;
	if (Persist)
	{
		const FString DataRoot = Persist->GetDataRootDirectory();
		if (!DataRoot.IsEmpty() && FPaths::DirectoryExists(DataRoot))
		{
			BuildTreeForRoot(DataRoot, TEXT("persistence"), GMaxDirDepth);
		}
	}

	const FString ProjSaved = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor"));
	if (FPaths::DirectoryExists(ProjSaved))
	{
		{
			TSharedPtr<FUnrealAiDebugListEntry> Sep = MakeShared<FUnrealAiDebugListEntry>();
			Sep->DisplayName = TEXT("--- project Saved/UnrealAiEditor ---");
			Sep->FullPath.Reset();
			Sep->bIsDirectory = false;
			Sep->Depth = 0;
			FileEntries.Add(Sep);
		}
		BuildTreeForRoot(ProjSaved, TEXT("Saved/UnrealAiEditor"), GMaxDirDepth);
	}

	RebuildOpenChatHighlightCache();
	if (FileList.IsValid())
	{
		FileList->RequestListRefresh();
	}
}

void SUnrealAiEditorDebugTab::RebuildOpenChatHighlightCache()
{
	OpenChatExactPathsNorm.Reset();
	OpenChatDirectoryPrefixesNorm.Reset();
	IUnrealAiPersistence* const P = BackendRegistry.IsValid() ? BackendRegistry->GetPersistence() : nullptr;
	if (!P)
	{
		return;
	}
	const FString Proj = UnrealAiProjectId::GetCurrentProjectId();
	const FString ThreadsRoot = FPaths::Combine(P->GetDataRootDirectory(), TEXT("chats"), Proj, TEXT("threads"));
	TArray<FGuid> Open;
	FUnrealAiEditorModule::GetOpenAgentChatThreadIds(Open);
	for (const FGuid& G : Open)
	{
		if (!G.IsValid())
		{
			continue;
		}
		const FString Tid = G.ToString(EGuidFormats::DigitsWithHyphens);
		const FString Slug = P->GetThreadStorageSlug(Proj, Tid);
		if (Slug.IsEmpty())
		{
			continue;
		}
		const FString Ctx = UnrealAiNormalizeDebugPath(FPaths::Combine(ThreadsRoot, Slug + TEXT("-context.json")));
		const FString Conv =
			UnrealAiNormalizeDebugPath(FPaths::Combine(ThreadsRoot, Slug + TEXT("-conversation.json")));
		const FString LegDir = UnrealAiNormalizeDebugPath(FPaths::Combine(ThreadsRoot, Slug));
		OpenChatExactPathsNorm.Add(Ctx);
		OpenChatExactPathsNorm.Add(Conv);
		OpenChatDirectoryPrefixesNorm.Add(LegDir);
	}
}

bool SUnrealAiEditorDebugTab::IsPathHighlighted(const FString& FullPathNormalized) const
{
	if (FullPathNormalized.IsEmpty())
	{
		return false;
	}
	if (OpenChatExactPathsNorm.Contains(FullPathNormalized))
	{
		return true;
	}
	for (const FString& Prefix : OpenChatDirectoryPrefixesNorm)
	{
		if (FullPathNormalized.Equals(Prefix, ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (FullPathNormalized.Len() > Prefix.Len() && FullPathNormalized.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			const TCHAR C = FullPathNormalized[Prefix.Len()];
			if (C == TEXT('/') || C == TEXT('\\'))
			{
				return true;
			}
		}
	}
	return false;
}

void SUnrealAiEditorDebugTab::BuildTreeForRoot(const FString& RootPath, const FString& LabelForRoot, int32 MaxDepth)
{
	struct FWalk
	{
		static void Walk(
			const FString& Dir,
			int32 Depth,
			int32 MaxD,
			const FString& DisplayRoot,
			TArray<TSharedPtr<FUnrealAiDebugListEntry>>& Out)
		{
			if (Depth == 0)
			{
				TSharedPtr<FUnrealAiDebugListEntry> R = MakeShared<FUnrealAiDebugListEntry>();
				R->DisplayName = FString::Printf(TEXT("%s  (%s)"), *DisplayRoot, *Dir);
				R->FullPath = Dir;
				R->bIsDirectory = true;
				R->Depth = 0;
				Out.Add(R);
			}
			if (Depth >= MaxD)
			{
				return;
			}
			TArray<FString> SubDirs;
			IFileManager::Get().FindFiles(SubDirs, *(Dir / TEXT("*")), false, true);
			SubDirs.Sort();
			for (const FString& Sub : SubDirs)
			{
				const FString Full = Dir / Sub;
				TSharedPtr<FUnrealAiDebugListEntry> E = MakeShared<FUnrealAiDebugListEntry>();
				E->DisplayName = Sub;
				E->FullPath = Full;
				E->bIsDirectory = true;
				E->Depth = Depth + 1;
				Out.Add(E);
				Walk(Full, Depth + 1, MaxD, DisplayRoot, Out);
			}
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*")), true, false);
			Files.Sort();
			for (const FString& F : Files)
			{
				TSharedPtr<FUnrealAiDebugListEntry> E = MakeShared<FUnrealAiDebugListEntry>();
				E->DisplayName = F;
				E->FullPath = Dir / F;
				E->bIsDirectory = false;
				E->Depth = Depth + 1;
				Out.Add(E);
			}
		}
	};
	FWalk::Walk(RootPath, 0, MaxDepth, LabelForRoot, FileEntries);
}

void SUnrealAiEditorDebugTab::RefreshActiveSessionUi()
{
	const TSharedPtr<FUnrealAiChatUiSession> Sess = FUnrealAiEditorModule::GetActiveChatSession();
	if (Sess.IsValid())
	{
		const FString Tid = Sess->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
		SummaryLine = FText::Format(
			LOCTEXT("DbgActiveFmt", "thread_id:\n{0}\n\nUse Context / Conversation to load persisted JSON for this thread."),
			FText::FromString(Tid));
	}
	else
	{
		SummaryLine = LOCTEXT("DbgNoActive", "No active Agent Chat session. Open Agent Chat or focus a chat tab.");
	}
}

void SUnrealAiEditorDebugTab::OnRefreshClicked()
{
	RebuildFileList();
	RefreshActiveSessionUi();
}

void SUnrealAiEditorDebugTab::OnAutoRefreshToggled(const ECheckBoxState State)
{
	bAutoRefresh = (State == ECheckBoxState::Checked);
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (bAutoRefresh)
	{
		AutoRefreshAccumSeconds = 0.f;
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(this, &SUnrealAiEditorDebugTab::OnAutoRefreshTick));
	}
}

ECheckBoxState SUnrealAiEditorDebugTab::GetAutoRefreshCheckState() const
{
	return bAutoRefresh ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool SUnrealAiEditorDebugTab::OnAutoRefreshTick(float DeltaSeconds)
{
	if (!bAutoRefresh)
	{
		return false;
	}
	AutoRefreshAccumSeconds += DeltaSeconds;
	if (AutoRefreshAccumSeconds >= GAutoRefreshSeconds)
	{
		AutoRefreshAccumSeconds = 0.f;
		RebuildFileList();
		RefreshActiveSessionUi();
	}
	return true;
}

TSharedRef<ITableRow> SUnrealAiEditorDebugTab::OnGenerateRow(
	TSharedPtr<FUnrealAiDebugListEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const float Pad = Item.IsValid() ? 8.f + static_cast<float>(Item->Depth) * 12.f : 8.f;
	const FSlateColor Col = Item.IsValid() && Item->bIsDirectory
		? static_cast<FSlateColor>(FUnrealAiEditorStyle::ColorDebugNavFolder())
		: static_cast<FSlateColor>(FUnrealAiEditorStyle::ColorTextPrimary());

	const FString PathNorm =
		Item.IsValid() && !Item->FullPath.IsEmpty() ? UnrealAiNormalizeDebugPath(Item->FullPath) : FString();
	const bool bOpenChatRow = IsPathHighlighted(PathNorm);

	return SNew(STableRow<TSharedPtr<FUnrealAiDebugListEntry>>, OwnerTable)
		[
			SNew(SBorder)
			.Padding(FMargin(0))
			.BorderBackgroundColor(
				bOpenChatRow ? FLinearColor(0.12f, 0.26f, 0.48f, 1.f) : FLinearColor::Transparent)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(Pad, 4, 8, 4)
				[
					SNew(STextBlock)
					.Text(Item.IsValid() ? FText::FromString(Item->DisplayName) : FText::GetEmpty())
					.ColorAndOpacity(Col)
					.Font(FUnrealAiEditorStyle::FontBodySmall())
				]
			]
		];
}

void SUnrealAiEditorDebugTab::OnSelectionChanged(
	TSharedPtr<FUnrealAiDebugListEntry> Item,
	const ESelectInfo::Type SelectInfo)
{
	(void)SelectInfo;
	SelectedEntry = Item;
	if (!Item.IsValid() || Item->FullPath.IsEmpty() || Item->bIsDirectory)
	{
		const FText Msg = Item.IsValid() && Item->bIsDirectory && !Item->FullPath.IsEmpty()
			? FText::Format(
				LOCTEXT("DbgDirSel", "Directory: {0}\n\nSelect a file to view contents."),
				FText::FromString(Item->FullPath))
			: LOCTEXT("DbgPick", "Select a file from the list.");
		if (InspectorPanel.IsValid())
		{
			InspectorPanel->SetInspectorText(Msg.ToString());
		}
		return;
	}

	if (InspectorPanel.IsValid())
	{
		InspectorPanel->InspectFilePath(Item->FullPath);
	}
}

void SUnrealAiEditorDebugTab::OnLoadActiveContext()
{
	IUnrealAiPersistence* Persist =
		BackendRegistry.IsValid() ? BackendRegistry->GetPersistence() : nullptr;
	const TSharedPtr<FUnrealAiChatUiSession> Sess = FUnrealAiEditorModule::GetActiveChatSession();
	if (!Persist || !Sess.IsValid())
	{
		if (InspectorPanel.IsValid())
		{
			InspectorPanel->SetInspectorText(LOCTEXT("DbgNoCtx", "Persistence or active session unavailable.").ToString());
		}
		return;
	}
	const FString Proj = UnrealAiProjectId::GetCurrentProjectId();
	const FString Tid = Sess->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	FString Json;
	if (!Persist->LoadThreadContextJson(Proj, Tid, Json))
	{
		if (InspectorPanel.IsValid())
		{
			InspectorPanel->SetInspectorText(LOCTEXT("DbgCtxMissing", "No context.json on disk for this thread yet.").ToString());
		}
		return;
	}
	if (InspectorPanel.IsValid())
	{
		InspectorPanel->SetInspectorText(Json);
	}
}

void SUnrealAiEditorDebugTab::OnLoadActiveConversation()
{
	IUnrealAiPersistence* Persist =
		BackendRegistry.IsValid() ? BackendRegistry->GetPersistence() : nullptr;
	const TSharedPtr<FUnrealAiChatUiSession> Sess = FUnrealAiEditorModule::GetActiveChatSession();
	if (!Persist || !Sess.IsValid())
	{
		if (InspectorPanel.IsValid())
		{
			InspectorPanel->SetInspectorText(LOCTEXT("DbgNoConv", "Persistence or active session unavailable.").ToString());
		}
		return;
	}
	const FString Proj = UnrealAiProjectId::GetCurrentProjectId();
	const FString Tid = Sess->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	FString Json;
	if (!Persist->LoadThreadConversationJson(Proj, Tid, Json))
	{
		if (InspectorPanel.IsValid())
		{
			InspectorPanel->SetInspectorText(LOCTEXT("DbgConvMissing", "No conversation.json on disk for this thread yet.").ToString());
		}
		return;
	}
	if (InspectorPanel.IsValid())
	{
		InspectorPanel->SetInspectorText(Json);
	}
}

void SUnrealAiEditorDebugTab::OnCopyInspectorClicked()
{
	if (InspectorPanel.IsValid())
	{
		InspectorPanel->CopyCurrentToClipboard();
	}
}

FReply SUnrealAiEditorDebugTab::OnDeleteAllLocalChatDataClicked()
{
	IUnrealAiPersistence* const P = BackendRegistry.IsValid() ? BackendRegistry->GetPersistence() : nullptr;
	if (!P)
	{
		return FReply::Handled();
	}
	const FString ChatsRoot = FPaths::Combine(P->GetDataRootDirectory(), TEXT("chats"));
	const FString SavedPlugin = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor"));
	const FText Msg = FText::Format(
		LOCTEXT(
			"DbgDelConfirm",
			"Delete ALL local chat data?\n\nThis removes:\n- {0}\n- {1}\n\nKeeps: settings (models, API keys, usage stats).\n\n"
			"Open Agent Chat tabs may still show cached messages until you start a new chat or restart the editor."),
		FText::FromString(ChatsRoot),
		FText::FromString(SavedPlugin));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Msg) != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}
	FString Err;
	if (!P->DeleteAllLocalChatData(Err))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Err.IsEmpty() ? TEXT("Delete failed.") : Err));
		return FReply::Handled();
	}
	const FString Proj = UnrealAiProjectId::GetCurrentProjectId();
	P->SaveOpenChatTabsState(Proj, {});
	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->WipeAllSessionsInMemory();
	}
	RebuildFileList();
	RefreshActiveSessionUi();
	FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("DbgDelOk", "Local chat data deleted."));
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
