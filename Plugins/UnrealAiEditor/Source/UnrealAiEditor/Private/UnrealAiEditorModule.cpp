#include "UnrealAiEditorModule.h"

#include "Modules/ModuleManager.h"
#include "Async/Async.h"
#include "App/UnrealAiEditorCommands.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiContextDragDrop.h"
#include "Context/UnrealAiEditorContextQueries.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Tabs/SUnrealAiEditorChatTab.h"
#include "Tabs/SUnrealAiEditorHelpTab.h"
#include "Tabs/SUnrealAiEditorQuickStartTab.h"
#include "Tabs/SUnrealAiEditorSettingsTab.h"
#include "UnrealAiEditorSettings.h"
#include "UnrealAiEditorTabIds.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/SDockingTabStack.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "IContentBrowserSingleton.h"
#include "LevelEditor.h"
#include "Selection.h"
#include "Styling/AppStyle.h"
#include "ToolMenuEntry.h"
#include "ToolMenu.h"
#include "ISettingsModule.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"
#include "WorkspaceMenuStructure.h"
#include "Misc/Guid.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

static FUnrealAiEditorModule* GUnrealAiModule = nullptr;

static void RegisterUnrealAiEditorKeyBindings()
{
	static bool bDone = false;
	if (bDone)
	{
		return;
	}
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	const TSharedPtr<FUICommandList> Cmd = LevelEditorModule.GetGlobalLevelEditorActions();
	if (!Cmd.IsValid())
	{
		return;
	}
	const FUnrealAiEditorCommands& C = FUnrealAiEditorCommands::Get();
	Cmd->MapAction(
		C.OpenChatTab,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ChatTab);
		}));
	Cmd->MapAction(
		C.OpenSettingsTab,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::SettingsTab);
		}));
	Cmd->MapAction(
		C.OpenQuickStartTab,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::QuickStartTab);
		}));
	Cmd->MapAction(
		C.OpenHelpTab,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::HelpTab);
		}));
	bDone = true;
}

void FUnrealAiEditorModule::StartupModule()
{
	GUnrealAiModule = this;

	BackendRegistry = MakeShared<FUnrealAiBackendRegistry>();

	FUnrealAiEditorStyle::Initialize();

	FUnrealAiEditorCommands::Register();

	const TSharedPtr<FUnrealAiBackendRegistry> Reg = BackendRegistry;

	RegisterTabs(Reg);
	RegisterMenus();
	RegisterSettings();
	RegisterOpenChatOnStartup();
}

void FUnrealAiEditorModule::ShutdownModule()
{
	if (BackendRegistry.IsValid())
	{
		if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
		{
			Ctx->FlushAllSessionsToDisk();
		}
	}

	UnregisterSettings();
	UnregisterOpenChatOnStartup();
	UnregisterMenus();
	UnregisterTabs();

	FUnrealAiEditorCommands::Unregister();

	FUnrealAiEditorStyle::Shutdown();

	BackendRegistry.Reset();

	GUnrealAiModule = nullptr;
}

FUnrealAiEditorModule& FUnrealAiEditorModule::Get()
{
	check(GUnrealAiModule);
	return *GUnrealAiModule;
}

TSharedPtr<FUnrealAiBackendRegistry> FUnrealAiEditorModule::GetBackendRegistry()
{
	return GUnrealAiModule ? GUnrealAiModule->BackendRegistry : nullptr;
}

void FUnrealAiEditorModule::SetActiveChatSession(TSharedPtr<FUnrealAiChatUiSession> Session)
{
	if (GUnrealAiModule)
	{
		GUnrealAiModule->ActiveChatSession = Session;
	}
}

TSharedPtr<FUnrealAiChatUiSession> FUnrealAiEditorModule::GetActiveChatSession()
{
	return GUnrealAiModule ? GUnrealAiModule->ActiveChatSession.Pin() : nullptr;
}

void FUnrealAiEditorModule::NotifyContextAttachmentsChanged()
{
	OnContextAttachmentsChanged().Broadcast();
}

FSimpleMulticastDelegate& FUnrealAiEditorModule::OnContextAttachmentsChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

#if WITH_EDITOR
static void UnrealAi_RegisterContextMenu_AddToChat(const FName MenuName)
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(MenuName);
	if (!Menu)
	{
		return;
	}
	FToolMenuSection& Section = Menu->AddSection(
		"UnrealAiContext",
		LOCTEXT("UnrealAiHeading", "Unreal AI"),
		FToolMenuInsert(NAME_None, EToolMenuInsertType::First));
	Section.AddMenuEntry(
		FName(*FString::Printf(TEXT("UnrealAiAddToContext_%s"), *MenuName.ToString().Replace(TEXT("."), TEXT("_")))),
		LOCTEXT("AddToContext", "Add to context"),
		LOCTEXT(
			"AddToContextTip",
			"Add the current selection to the active Agent Chat thread (same as dragging into the chat)"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
		FUIAction(FExecuteAction::CreateLambda(
			[MenuName]()
			{
				const TSharedPtr<FUnrealAiBackendRegistry> Reg = FUnrealAiEditorModule::GetBackendRegistry();
				const TSharedPtr<FUnrealAiChatUiSession> Session = FUnrealAiEditorModule::GetActiveChatSession();
				if (!Reg.IsValid() || !Session.IsValid())
				{
					return;
				}
				TArray<FContextAttachment> Atts;
				if (MenuName == FName(TEXT("ContentBrowser.AssetContextMenu")))
				{
					if (FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
					{
						FContentBrowserModule& CBM = FModuleManager::GetModuleChecked<FContentBrowserModule>(
							TEXT("ContentBrowser"));
						TArray<FAssetData> Selected;
						CBM.Get().GetSelectedAssets(Selected);
						for (const FAssetData& AD : Selected)
						{
							if (AD.IsValid())
							{
								Atts.Add(UnrealAiEditorContextQueries::AttachmentFromAssetData(AD));
							}
						}
					}
				}
				else if (
					MenuName == FName(TEXT("LevelEditor.ActorContextMenu"))
					|| MenuName == FName(TEXT("LevelEditor.SceneOutlinerContextMenu")))
				{
					if (GEditor)
					{
						if (USelection* ActorSelection = GEditor->GetSelectedActors())
						{
							for (FSelectionIterator It(*ActorSelection); It; ++It)
							{
								if (AActor* A = Cast<AActor>(*It))
								{
									Atts.Add(UnrealAiEditorContextQueries::AttachmentFromActor(A));
								}
							}
						}
					}
				}
				if (Atts.Num() == 0)
				{
					return;
				}
				UnrealAiContextDragDrop::AddAttachmentsToActiveChat(Reg, Session, Atts);
				FUnrealAiEditorModule::NotifyContextAttachmentsChanged();
			})));
}
#endif

namespace UnrealAiAgentChatTabSpawn
{
	struct FContentBox
	{
		TSharedPtr<SUnrealAiEditorChatTab> ChatTab;
	};
}

static TSharedPtr<SDockTab> FindParentDockTabForWidget(const TSharedRef<const SWidget>& Widget)
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

static TSharedRef<SDockTab> SpawnAgentChatDockTab(
	const TSharedPtr<FUnrealAiBackendRegistry>& Reg,
	const FSpawnTabArgs& Args)
{
	(void)Args;
	TSharedPtr<UnrealAiAgentChatTabSpawn::FContentBox> Box = MakeShared<UnrealAiAgentChatTabSpawn::FContentBox>();
	TSharedPtr<SUnrealAiEditorChatTab> ChatTab;
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("ChatTabLabel", "Agent Chat"))
		.OnExtendContextMenu(SDockTab::FExtendContextMenu::CreateLambda(
			[Box](FMenuBuilder& MenuBuilder)
			{
				const TSharedPtr<SUnrealAiEditorChatTab> Chat = Box->ChatTab;
				if (!Chat.IsValid())
				{
					return;
				}
				MenuBuilder.BeginSection("UnrealAiChat", LOCTEXT("UnrealAiChatCtxSection", "Agent Chat"));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CtxNewChat", "New chat"),
					LOCTEXT("CtxNewChatTip", "Open a new Agent Chat tab to the right of this one."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Chat]()
					{
						Chat->MenuNewChat();
					})));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CtxExportChat", "Export chat…"),
					LOCTEXT("CtxExportChatTip", "Save the transcript as a text file."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Chat]()
					{
						Chat->MenuExportChat();
					})));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CtxCopyChat", "Copy chat to clipboard"),
					LOCTEXT("CtxCopyChatTip", "Copy the transcript as plain text."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Chat]()
					{
						Chat->MenuCopyChatToClipboard();
					})));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CtxDeleteChat", "Delete chat"),
					LOCTEXT(
						"CtxDeleteChatTip",
						"Remove this conversation from memory and delete persisted thread data on disk."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Chat]()
					{
						Chat->MenuDeleteChat();
					})));
				MenuBuilder.EndSection();
			}))
		[
			SAssignNew(ChatTab, SUnrealAiEditorChatTab).BackendRegistry(Reg)
		];
	Box->ChatTab = ChatTab;
	return Tab;
}

void FUnrealAiEditorModule::OpenNewAgentChatTabBeside(const TSharedPtr<SWidget>& FromWidget)
{
	if (!FromWidget.IsValid())
	{
		return;
	}
	const TSharedPtr<FUnrealAiBackendRegistry> Reg = GetBackendRegistry();
	if (!Reg.IsValid())
	{
		return;
	}
	TSharedPtr<SDockTab> ParentDock = FindParentDockTabForWidget(FromWidget.ToSharedRef());
	TSharedPtr<SWindow> OwnerWindow;
	if (ParentDock.IsValid())
	{
		OwnerWindow = ParentDock->GetParentWindow();
	}
	if (!OwnerWindow.IsValid())
	{
		OwnerWindow = FGlobalTabmanager::Get()->GetRootWindow();
	}
	const FSpawnTabArgs Args(OwnerWindow, FTabId(UnrealAiEditorTabIds::ChatTab, INDEX_NONE));
	TSharedRef<SDockTab> NewTab = SpawnAgentChatDockTab(Reg, Args);
	// Note: SDockTab::SetLayoutIdentifier is protected (friend FTabManager only). Extra Agent Chat tabs
	// are spawned manually so FTabManager::SpawnTab is not used (nomad spawner tracks a single SpawnedTabPtr).

	if (TSharedPtr<SDockingTabStack> Stack = ParentDock.IsValid() ? ParentDock->GetParentDockTabStack() : nullptr)
	{
		int32 InsertIdx = INDEX_NONE;
		const TSlotlessChildren<SDockTab>& Tabs = Stack->GetTabs();
		if (ParentDock.IsValid())
		{
			const FTabId CurrentId = ParentDock->GetLayoutIdentifier();
			for (int32 i = 0; i < Tabs.Num(); ++i)
			{
				if (Tabs[i]->GetLayoutIdentifier() == CurrentId)
				{
					InsertIdx = i + 1;
					break;
				}
			}
		}
		Stack->OpenTab(NewTab, InsertIdx, false);
	}
	else
	{
		FGlobalTabmanager::Get()->InsertNewDocumentTab(
			UnrealAiEditorTabIds::ChatTab,
			FTabManager::ESearchPreference::PreferLiveTab,
			NewTab);
	}
}

void FUnrealAiEditorModule::RegisterTabs(const TSharedPtr<FUnrealAiBackendRegistry>& Reg)
{
	const auto SpawnChat = [Reg](const FSpawnTabArgs& Args) -> TSharedRef<SDockTab>
	{
		return SpawnAgentChatDockTab(Reg, Args);
	};

	const auto SpawnSettings = [Reg](const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Label(LOCTEXT("SettingsTabLabel", "AI Settings"))
			[
				SNew(SUnrealAiEditorSettingsTab).BackendRegistry(Reg)
			];
	};

	const auto SpawnQuick = [](const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Label(LOCTEXT("QuickTabLabel", "Quick Start"))
			[
				SNew(SUnrealAiEditorQuickStartTab)
			];
	};

	const auto SpawnHelp = [](const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Label(LOCTEXT("HelpTabLabel", "Help"))
			[
				SNew(SUnrealAiEditorHelpTab)
			];
	};

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
							 UnrealAiEditorTabIds::ChatTab,
							 FOnSpawnTab::CreateLambda(SpawnChat))
		.SetDisplayName(LOCTEXT("ChatTab", "Agent Chat"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
							 UnrealAiEditorTabIds::SettingsTab,
							 FOnSpawnTab::CreateLambda(SpawnSettings))
		.SetDisplayName(LOCTEXT("SettingsTab", "AI Settings"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
							 UnrealAiEditorTabIds::QuickStartTab,
							 FOnSpawnTab::CreateLambda(SpawnQuick))
		.SetDisplayName(LOCTEXT("QuickStartTab", "Quick Start"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
							 UnrealAiEditorTabIds::HelpTab,
							 FOnSpawnTab::CreateLambda(SpawnHelp))
		.SetDisplayName(LOCTEXT("HelpTab", "Help"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
}

void FUnrealAiEditorModule::UnregisterTabs()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealAiEditorTabIds::ChatTab);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealAiEditorTabIds::SettingsTab);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealAiEditorTabIds::QuickStartTab);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealAiEditorTabIds::HelpTab);
}

void FUnrealAiEditorModule::RegisterMenus()
{
	static const FName UnrealAiOwner("UnrealAiEditorOwner");
	FToolMenuOwnerScoped OwnerScoped(UnrealAiOwner);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda(
		[]()
		{
			// Bind commands before toolbar/menu entries that reference the command list.
			RegisterUnrealAiEditorKeyBindings();

			UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
			FToolMenuSection& Section = WindowMenu->FindOrAddSection("WindowGlobal");
			Section.AddSubMenu(
				"UnrealAiRoot",
				LOCTEXT("UnrealAiRoot", "Unreal AI"),
				LOCTEXT("UnrealAiRootTip", "Unreal AI Editor windows"),
				FNewToolMenuDelegate::CreateLambda(
					[](UToolMenu* SubMenu)
					{
						FToolMenuSection& S = SubMenu->AddSection("UnrealAiItems");
						S.AddMenuEntry(
							"UnrealAiChat",
							LOCTEXT("MenuChat", "Agent Chat"),
							LOCTEXT("MenuChatTip", "Open Agent Chat"),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ChatTab);
							})));
						S.AddMenuEntry(
							"UnrealAiSettings",
							LOCTEXT("MenuSettings", "AI Settings"),
							LOCTEXT("MenuSettingsTip", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::SettingsTab);
							})));
						S.AddMenuEntry(
							"UnrealAiQuick",
							LOCTEXT("MenuQuick", "Quick Start"),
							LOCTEXT("MenuQuickTip", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::QuickStartTab);
							})));
						S.AddMenuEntry(
							"UnrealAiHelp",
							LOCTEXT("MenuHelp", "Help"),
							LOCTEXT("MenuHelpTip", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::HelpTab);
							})));
					}));

			UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
			FToolMenuSection& ToolsSection = ToolsMenu->FindOrAddSection("UnrealAiTools");
			ToolsSection.AddSubMenu(
				"UnrealAiToolsRoot",
				LOCTEXT("UnrealAiToolsRoot", "Unreal AI"),
				LOCTEXT("UnrealAiToolsRootTip", "Unreal AI Editor windows"),
				FNewToolMenuDelegate::CreateLambda(
					[](UToolMenu* SubMenu)
					{
						FToolMenuSection& S = SubMenu->AddSection("UnrealAiToolItems");
						S.AddMenuEntry(
							"UnrealAiChatT",
							LOCTEXT("MenuChatT", "Agent Chat"),
							LOCTEXT("MenuChatTipT", "Open Agent Chat"),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ChatTab);
							})));
						S.AddMenuEntry(
							"UnrealAiSettingsT",
							LOCTEXT("MenuSettingsT", "AI Settings"),
							LOCTEXT("MenuSettingsTipT", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::SettingsTab);
							})));
						S.AddMenuEntry(
							"UnrealAiQuickT",
							LOCTEXT("MenuQuickT", "Quick Start"),
							LOCTEXT("MenuQuickTipT", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::QuickStartTab);
							})));
						S.AddMenuEntry(
							"UnrealAiHelpT",
							LOCTEXT("MenuHelpT", "Help"),
							LOCTEXT("MenuHelpTipT", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::HelpTab);
							})));
					}));

			// Main Level Editor toolbar (top) — Nomad tabs only list under Window until opened once; this makes the UI discoverable.
			if (UToolMenu* ToolBarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar"))
			{
				FToolMenuSection& ToolBarSection = ToolBarMenu->FindOrAddSection("UnrealAiToolbar");
				ToolBarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
					FUnrealAiEditorCommands::Get().OpenChatTab,
					LOCTEXT("ToolbarUnrealAi", "Unreal AI"),
					LOCTEXT("ToolbarUnrealAiTip", "Open Agent Chat"),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Question"),
					NAME_None,
					TOptional<FName>(FName(TEXT("UnrealAiToolbarChat")))));
			}

#if WITH_EDITOR
			UnrealAi_RegisterContextMenu_AddToChat(FName(TEXT("ContentBrowser.AssetContextMenu")));
			UnrealAi_RegisterContextMenu_AddToChat(FName(TEXT("LevelEditor.ActorContextMenu")));
			UnrealAi_RegisterContextMenu_AddToChat(FName(TEXT("LevelEditor.SceneOutlinerContextMenu")));
#endif
		}));
}

void FUnrealAiEditorModule::UnregisterMenus()
{
	static const FName UnrealAiOwner("UnrealAiEditorOwner");
	UToolMenus::UnregisterOwner(UnrealAiOwner);
}

void FUnrealAiEditorModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings(
			"Project",
			"Plugins",
			"UnrealAiEditor",
			LOCTEXT("UnrealAiSettingsName", "Unreal AI Editor"),
			LOCTEXT("UnrealAiSettingsDesc", "Local-first AI plugin settings"),
			GetMutableDefault<UUnrealAiEditorSettings>());
	}
}

void FUnrealAiEditorModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "UnrealAiEditor");
	}
}

void FUnrealAiEditorModule::RegisterOpenChatOnStartup()
{
	OpenChatOnStartupHandle = FEditorDelegates::OnEditorInitialized.AddLambda([](double /*DeltaTime*/)
	{
		if (!GetDefault<UUnrealAiEditorSettings>()->bOpenAgentChatOnStartup)
		{
			return;
		}
		// Defer one game-thread pump so the global tab manager and layout are ready.
		AsyncTask(ENamedThreads::GameThread, []()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ChatTab);
		});
	});
}

void FUnrealAiEditorModule::UnregisterOpenChatOnStartup()
{
	if (OpenChatOnStartupHandle.IsValid())
	{
		FEditorDelegates::OnEditorInitialized.Remove(OpenChatOnStartupHandle);
		OpenChatOnStartupHandle.Reset();
	}
}

IMPLEMENT_MODULE(FUnrealAiEditorModule, UnrealAiEditor)

#undef LOCTEXT_NAMESPACE
