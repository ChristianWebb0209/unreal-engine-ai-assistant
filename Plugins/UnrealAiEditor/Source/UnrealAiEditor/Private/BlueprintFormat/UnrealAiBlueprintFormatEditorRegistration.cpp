#include "BlueprintFormat/UnrealAiBlueprintFormatEditorRegistration.h"

#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "HAL/IConsoleManager.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "ToolMenuEntry.h"

#define LOCTEXT_NAMESPACE "UnrealAiBlueprintFormatEditorRegistration"

namespace UnrealAiBlueprintFormatEditorRegistrationPriv
{
	static IConsoleObject* GFormatSelectionCmd = nullptr;

	static void RunFormatSelectionFromConsole(const TArray<FString>& Args)
	{
		if (!GEditor || Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("UnrealAi.BlueprintFormatSelection <blueprint_path> [graph_name] [layout_mode] [wire_knots] — Blueprint editor must be open."));
			return;
		}
		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("blueprint_path"), Args[0]);
		if (Args.Num() > 1)
		{
			J->SetStringField(TEXT("graph_name"), Args[1]);
		}
		if (Args.Num() > 2)
		{
			J->SetStringField(TEXT("layout_mode"), Args[2]);
		}
		if (Args.Num() > 3)
		{
			J->SetStringField(TEXT("wire_knots"), Args[3]);
		}
		const FUnrealAiToolInvocationResult R = UnrealAiDispatch_BlueprintFormatSelection(J);
		UE_LOG(LogTemp, Display, TEXT("UnrealAi.BlueprintFormatSelection: ok=%d %s"),
			R.bOk ? 1 : 0,
			R.bOk ? TEXT("") : *R.ErrorMessage);
	}
}

void UnrealAiBlueprintFormatEditorExtendBlueprintToolbar()
{
	static const FName MenuCandidates[] = {
		FName(TEXT("AssetEditor.BlueprintEditor.ToolBar")),
		FName(TEXT("AssetEditor.BlueprintEditor.MainToolBar")),
	};
	for (const FName MenuName : MenuCandidates)
	{
		UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(MenuName);
		if (!Toolbar)
		{
			continue;
		}
		FToolMenuSection& Section = Toolbar->FindOrAddSection(TEXT("UnrealAiBlueprintFormat"));
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			"UnrealAiFormatBlueprintSelection",
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				if (!GEditor)
				{
					return;
				}
				UAssetEditorSubsystem* Subsys = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				if (!Subsys)
				{
					return;
				}
				const TArray<UObject*> Assets = Subsys->GetAllEditedAssets();
				for (UObject* A : Assets)
				{
					if (UBlueprint* BP = Cast<UBlueprint>(A))
					{
						TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
						J->SetStringField(TEXT("blueprint_path"), BP->GetPathName());
						const FUnrealAiToolInvocationResult R = UnrealAiDispatch_BlueprintFormatSelection(J);
						if (!R.bOk)
						{
							UE_LOG(LogTemp, Warning, TEXT("Unreal AI format selection: %s"), *R.ErrorMessage);
						}
						return;
					}
				}
				UE_LOG(LogTemp, Warning, TEXT("Unreal AI format selection: no Blueprint asset editor is open."));
			})),
			LOCTEXT("FmtSelBtn", "AI: Format selection"),
			LOCTEXT("FmtSelTip", "Run bundled layout on the current Blueprint graph selection (editor must be open)."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.GameSettings"))));
		break;
	}
}

void UnrealAiBlueprintFormatEditorRegistrationStartup()
{
	using namespace UnrealAiBlueprintFormatEditorRegistrationPriv;
	if (!GFormatSelectionCmd)
	{
		GFormatSelectionCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("UnrealAi.BlueprintFormatSelection"),
			TEXT("Layout selected nodes. Args: blueprint_path [graph_name] [layout_mode] [wire_knots]. Requires Blueprint editor open."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				UnrealAiBlueprintFormatEditorRegistrationPriv::RunFormatSelectionFromConsole(Args);
			}),
			ECVF_Default);
	}
}

void UnrealAiBlueprintFormatEditorRegistrationShutdown()
{
	using namespace UnrealAiBlueprintFormatEditorRegistrationPriv;
	if (GFormatSelectionCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GFormatSelectionCmd);
		GFormatSelectionCmd = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
