#include "Tabs/SUnrealAiEditorDebugTab.h"

#include "UnrealAiEditorModule.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/UnrealAiProjectId.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/FileHelper.h"
#include "Serialization/Archive.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

static constexpr int32 GMaxDebugFileBytes = 512 * 1024;
static constexpr int32 GMaxDirDepth = 8;
static constexpr float GAutoRefreshSeconds = 3.f;

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
	InspectorContent = LOCTEXT("DbgPick", "Select a file from the list.");

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
							SNew(SCheckBox)
							.Style(FAppStyle::Get(), TEXT("ToggleButtonCheckbox"))
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
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 9))
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
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(InspectorText, SMultiLineEditableText)
									.IsReadOnly(true)
									.AutoWrapText(true)
									.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 9))
								]
							]
						]
					]
				]
			]
		];

	if (InspectorText.IsValid())
	{
		InspectorText->SetText(InspectorContent);
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

	if (FileList.IsValid())
	{
		FileList->RequestListRefresh();
	}
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

	return SNew(STableRow<TSharedPtr<FUnrealAiDebugListEntry>>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(Pad, 4, 8, 4)
			[
				SNew(STextBlock)
				.Text(Item.IsValid() ? FText::FromString(Item->DisplayName) : FText::GetEmpty())
				.ColorAndOpacity(Col)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Roboto"), 9))
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
		InspectorContent = Item.IsValid() && Item->bIsDirectory && !Item->FullPath.IsEmpty()
			? FText::Format(LOCTEXT("DbgDirSel", "Directory: {0}\n\nSelect a file to view contents."), FText::FromString(Item->FullPath))
			: LOCTEXT("DbgPick", "Select a file from the list.");
		if (InspectorText.IsValid())
		{
			InspectorText->SetText(InspectorContent);
		}
		return;
	}

	bool bTrunc = false;
	const FString Raw = LoadFileCapped(Item->FullPath, bTrunc);
	FString Body = Raw;
	const FString Low = Item->FullPath.ToLower();
	if (Low.EndsWith(TEXT(".json")))
	{
		Body = PrettyOrRawJson(Raw);
	}
	if (bTrunc)
	{
		Body += TEXT("\n\n--- truncated (size cap) ---");
	}
	InspectorContent = FText::FromString(Body);
	if (InspectorText.IsValid())
	{
		InspectorText->SetText(InspectorContent);
	}
}

void SUnrealAiEditorDebugTab::OnLoadActiveContext()
{
	IUnrealAiPersistence* Persist =
		BackendRegistry.IsValid() ? BackendRegistry->GetPersistence() : nullptr;
	const TSharedPtr<FUnrealAiChatUiSession> Sess = FUnrealAiEditorModule::GetActiveChatSession();
	if (!Persist || !Sess.IsValid())
	{
		InspectorContent = LOCTEXT("DbgNoCtx", "Persistence or active session unavailable.");
		if (InspectorText.IsValid())
		{
			InspectorText->SetText(InspectorContent);
		}
		return;
	}
	const FString Proj = UnrealAiProjectId::GetCurrentProjectId();
	const FString Tid = Sess->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	FString Json;
	if (!Persist->LoadThreadContextJson(Proj, Tid, Json))
	{
		InspectorContent = LOCTEXT("DbgCtxMissing", "No context.json on disk for this thread yet.");
		if (InspectorText.IsValid())
		{
			InspectorText->SetText(InspectorContent);
		}
		return;
	}
	InspectorContent = FText::FromString(PrettyOrRawJson(Json));
	if (InspectorText.IsValid())
	{
		InspectorText->SetText(InspectorContent);
	}
}

void SUnrealAiEditorDebugTab::OnLoadActiveConversation()
{
	IUnrealAiPersistence* Persist =
		BackendRegistry.IsValid() ? BackendRegistry->GetPersistence() : nullptr;
	const TSharedPtr<FUnrealAiChatUiSession> Sess = FUnrealAiEditorModule::GetActiveChatSession();
	if (!Persist || !Sess.IsValid())
	{
		InspectorContent = LOCTEXT("DbgNoConv", "Persistence or active session unavailable.");
		if (InspectorText.IsValid())
		{
			InspectorText->SetText(InspectorContent);
		}
		return;
	}
	const FString Proj = UnrealAiProjectId::GetCurrentProjectId();
	const FString Tid = Sess->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	FString Json;
	if (!Persist->LoadThreadConversationJson(Proj, Tid, Json))
	{
		InspectorContent = LOCTEXT("DbgConvMissing", "No conversation.json on disk for this thread yet.");
		if (InspectorText.IsValid())
		{
			InspectorText->SetText(InspectorContent);
		}
		return;
	}
	InspectorContent = FText::FromString(PrettyOrRawJson(Json));
	if (InspectorText.IsValid())
	{
		InspectorText->SetText(InspectorContent);
	}
}

void SUnrealAiEditorDebugTab::OnCopyInspectorClicked()
{
	const FString S = InspectorContent.ToString();
	if (!S.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*S);
	}
}

FString SUnrealAiEditorDebugTab::PrettyOrRawJson(const FString& Raw) const
{
	TSharedPtr<FJsonObject> Obj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out, 2);
		if (FJsonSerializer::Serialize(Obj.ToSharedRef(), W))
		{
			return Out;
		}
	}
	TSharedPtr<FJsonValue> Val;
	const TSharedRef<TJsonReader<>> Reader2 = TJsonReaderFactory<>::Create(Raw);
	if (FJsonSerializer::Deserialize(Reader2, Val) && Val.IsValid())
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out, 2);
		if (FJsonSerializer::Serialize(Val.ToSharedRef(), TEXT(""), W))
		{
			return Out;
		}
	}
	return Raw;
}

FString SUnrealAiEditorDebugTab::LoadFileCapped(const FString& Path, bool& bOutTruncated) const
{
	bOutTruncated = false;
	if (!FPaths::FileExists(Path))
	{
		return FString::Printf(TEXT("(file not found: %s)"), *Path);
	}
	const int64 Sz = IFileManager::Get().FileSize(*Path);
	if (Sz < 0)
	{
		return TEXT("(could not read file size)");
	}
	if (Sz > GMaxDebugFileBytes)
	{
		bOutTruncated = true;
		FArchive* Reader = IFileManager::Get().CreateFileReader(*Path);
		if (!Reader)
		{
			return TEXT("(open failed)");
		}
		TArray<uint8> Buf;
		Buf.SetNumUninitialized(GMaxDebugFileBytes);
		Reader->Serialize(Buf.GetData(), GMaxDebugFileBytes);
		delete Reader;
		const FUTF8ToTCHAR Utf8(reinterpret_cast<const ANSICHAR*>(Buf.GetData()), Buf.Num());
		return FString(Utf8.Length(), Utf8.Get());
	}
	FString S;
	if (FFileHelper::LoadFileToString(S, *Path))
	{
		return S;
	}
	return TEXT("(read failed)");
}

#undef LOCTEXT_NAMESPACE
