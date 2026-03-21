#include "UnrealAiEditorModule.h"

#include "Modules/ModuleManager.h"
#include "App/UnrealAiEditorCommands.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/IAgentContextService.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Tabs/SUnrealAiEditorApiModelsTab.h"
#include "Tabs/SUnrealAiEditorChatTab.h"
#include "Tabs/SUnrealAiEditorHelpTab.h"
#include "Tabs/SUnrealAiEditorQuickStartTab.h"
#include "Tabs/SUnrealAiEditorSettingsTab.h"
#include "UnrealAiEditorSettings.h"
#include "UnrealAiEditorTabIds.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "ISettingsModule.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
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
		C.OpenApiModelsTab,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ApiModelsTab);
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

void FUnrealAiEditorModule::RegisterTabs(const TSharedPtr<FUnrealAiBackendRegistry>& Reg)
{
	const auto SpawnChat = [Reg](const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Label(LOCTEXT("ChatTabLabel", "Agent Chat"))
			[
				SNew(SUnrealAiEditorChatTab).BackendRegistry(Reg)
			];
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

	const auto SpawnApi = [Reg](const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Label(LOCTEXT("ApiTabLabel", "API Keys & Models"))
			[
				SNew(SUnrealAiEditorApiModelsTab).BackendRegistry(Reg)
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
							 UnrealAiEditorTabIds::ApiModelsTab,
							 FOnSpawnTab::CreateLambda(SpawnApi))
		.SetDisplayName(LOCTEXT("ApiModelsTab", "API Keys & Models"))
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
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealAiEditorTabIds::ApiModelsTab);
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
							"UnrealAiApi",
							LOCTEXT("MenuApi", "API Keys & Models"),
							LOCTEXT("MenuApiTip", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ApiModelsTab);
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
							"UnrealAiApiT",
							LOCTEXT("MenuApiT", "API Keys & Models"),
							LOCTEXT("MenuApiTipT", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ApiModelsTab);
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

			RegisterUnrealAiEditorKeyBindings();
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

IMPLEMENT_MODULE(FUnrealAiEditorModule, UnrealAiEditor)

#undef LOCTEXT_NAMESPACE
