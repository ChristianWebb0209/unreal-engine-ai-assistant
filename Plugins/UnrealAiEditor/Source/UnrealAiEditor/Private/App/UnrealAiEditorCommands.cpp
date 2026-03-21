#include "App/UnrealAiEditorCommands.h"

#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

FUnrealAiEditorCommands::FUnrealAiEditorCommands()
	: TCommands<FUnrealAiEditorCommands>(
		  TEXT("UnrealAiEditor"),
		  NSLOCTEXT("Contexts", "UnrealAiEditor", "Unreal AI Editor"),
		  NAME_None,
		  FAppStyle::GetAppStyleSetName())
{
}

void FUnrealAiEditorCommands::RegisterCommands()
{
	UI_COMMAND(
		OpenChatTab,
		"Agent Chat",
		"Open the Agent Chat window.",
		EUserInterfaceActionType::Button,
		FInputChord(EKeys::K, EModifierKey::Control));
	UI_COMMAND(OpenSettingsTab, "AI Settings", "Open full AI settings.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenApiModelsTab, "API Keys & Models", "Configure API keys and default models.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenQuickStartTab, "Quick Start", "Open Quick Start guide.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenHelpTab, "Help", "Open help.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
