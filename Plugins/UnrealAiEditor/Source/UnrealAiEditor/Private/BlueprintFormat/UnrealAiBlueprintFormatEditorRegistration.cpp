#include "BlueprintFormat/UnrealAiBlueprintFormatEditorRegistration.h"

#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/IConsoleManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "Widgets/Input/SCheckBox.h"
#include "ToolMenus.h"
#include "ToolMenuEntry.h"
#include "UnrealAiEditorSettings.h"

#include "BlueprintFormat/UnrealAiBlueprintGraphFocusSelection.h"

#define LOCTEXT_NAMESPACE "UnrealAiBlueprintFormatEditorRegistration"

namespace UnrealAiBlueprintFormatEditorRegistrationPriv
{
	static IConsoleObject* GFormatSelectionCmd = nullptr;

	static TSharedRef<SWidget> BuildFormatOptionsMenu()
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("FmtOptsHeading", "Blueprint formatting"));
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("CommentsMenu", "Comment synthesis"),
				LOCTEXT("CommentsMenuTip", "Auto region comments when the formatter adds boxes"),
				FNewMenuDelegate::CreateLambda([](FMenuBuilder& Sub)
				{
					auto CommentsAction = [](EUnrealAiBlueprintCommentsMode M) -> FUIAction
					{
						return FUIAction(
							FExecuteAction::CreateLambda([M]()
							{
								UUnrealAiEditorSettings* St = GetMutableDefault<UUnrealAiEditorSettings>();
								St->BlueprintCommentsMode = M;
								St->SaveConfig();
							}),
							FCanExecuteAction(),
							FGetActionCheckState::CreateLambda([M]()
							{
								const UUnrealAiEditorSettings* St = GetDefault<UUnrealAiEditorSettings>();
								return St->BlueprintCommentsMode == M ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							}));
					};
					Sub.AddMenuEntry(
						LOCTEXT("CommOff", "Off"),
						LOCTEXT("CommOffTip", "No auto region comments"),
						FSlateIcon(),
						CommentsAction(EUnrealAiBlueprintCommentsMode::Off),
						NAME_None,
						EUserInterfaceActionType::RadioButton);
					Sub.AddMenuEntry(
						LOCTEXT("CommMin", "Minimal"),
						LOCTEXT("CommMinTip", "Light comment synthesis"),
						FSlateIcon(),
						CommentsAction(EUnrealAiBlueprintCommentsMode::Minimal),
						NAME_None,
						EUserInterfaceActionType::RadioButton);
					Sub.AddMenuEntry(
						LOCTEXT("CommVerb", "Verbose"),
						LOCTEXT("CommVerbTip", "More comment regions when the formatter adds boxes"),
						FSlateIcon(),
						CommentsAction(EUnrealAiBlueprintCommentsMode::Verbose),
						NAME_None,
						EUserInterfaceActionType::RadioButton);
				}));
			MenuBuilder.AddMenuEntry(
				LOCTEXT("WireKnots", "Wire knots"),
				LOCTEXT("WireKnotsTip", "Reroute knots on long data wires (best-effort)"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([]()
					{
						UUnrealAiEditorSettings* St = GetMutableDefault<UUnrealAiEditorSettings>();
						St->bBlueprintFormatUseWireKnots = !St->bBlueprintFormatUseWireKnots;
						St->SaveConfig();
					}),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([]()
					{
						return GetDefault<UUnrealAiEditorSettings>()->bBlueprintFormatUseWireKnots ? ECheckBoxState::Checked
																									: ECheckBoxState::Unchecked;
					})),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("PreservePositions", "Preserve existing positions"),
				LOCTEXT("PreservePositionsTip", "Skip repositioning nodes that already have non-zero coordinates"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([]()
					{
						UUnrealAiEditorSettings* St = GetMutableDefault<UUnrealAiEditorSettings>();
						St->bBlueprintFormatPreserveExistingPositions = !St->bBlueprintFormatPreserveExistingPositions;
						St->SaveConfig();
					}),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([]()
					{
						return GetDefault<UUnrealAiEditorSettings>()->bBlueprintFormatPreserveExistingPositions
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ReflowComments", "Reflow comment boxes"),
				LOCTEXT("ReflowCommentsTip", "Resize comment nodes to fit members after layout"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([]()
					{
						UUnrealAiEditorSettings* St = GetMutableDefault<UUnrealAiEditorSettings>();
						St->bBlueprintFormatReflowCommentsByGeometry = !St->bBlueprintFormatReflowCommentsByGeometry;
						St->SaveConfig();
					}),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([]()
					{
						return GetDefault<UUnrealAiEditorSettings>()->bBlueprintFormatReflowCommentsByGeometry
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);
		}
		MenuBuilder.EndSection();
		return MenuBuilder.MakeWidget();
	}

	static void RunFormatSelectionFromConsole(const TArray<FString>& Args)
	{
		if (!GEditor || Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("UnrealAi.BlueprintFormatSelection <blueprint_path> [graph_name] — uses Editor Preferences; Blueprint editor must be open."));
			return;
		}
		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("blueprint_path"), Args[0]);
		if (Args.Num() > 1)
		{
			J->SetStringField(TEXT("graph_name"), Args[1]);
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
		FToolMenuSection& Section = Toolbar->FindOrAddSection(TEXT("Compile"));
		{
			FToolMenuEntry FormatGraphComboEntry = FToolMenuEntry::InitComboButton(
				TEXT("UnrealAiFormatBlueprintUbergraph"),
				FToolUIActionChoice(FUIAction(
					FExecuteAction::CreateLambda([]()
					{
						UBlueprint* BP = nullptr;
						UEdGraph* Graph = nullptr;
						int32 Count = 0;
						if (!UnrealAiTryGetFocusedBlueprintUbergraphSelection(BP, Graph, Count) || !BP || !Graph)
						{
							return;
						}
						TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
						J->SetStringField(TEXT("blueprint_path"), BP->GetPathName());
						J->SetStringField(TEXT("graph_name"), Graph->GetName());
						const FUnrealAiToolInvocationResult R = UnrealAiDispatch_BlueprintFormatGraph(J);
						if (!R.bOk)
						{
							UE_LOG(LogTemp, Warning, TEXT("Unreal AI format blueprint (ubergraph): %s"), *R.ErrorMessage);
						}
					}),
					FCanExecuteAction::CreateLambda([]()
					{
						UBlueprint* BP = nullptr;
						UEdGraph* Graph = nullptr;
						int32 Count = 0;
						return UnrealAiTryGetFocusedBlueprintUbergraphSelection(BP, Graph, Count);
					}))),
				FNewToolMenuChoice(FOnGetContent::CreateLambda([]()
				{
					return UnrealAiBlueprintFormatEditorRegistrationPriv::BuildFormatOptionsMenu();
				})),
				LOCTEXT("FmtBlueprintBtn", "AI: Format graph"),
				LOCTEXT(
					"FmtBlueprintTip",
					"Run bundled layout on the Blueprint Ubergraph (Event Graph). Click the arrow for formatter options (same as Editor Preferences → Unreal AI Editor → Blueprint Formatting)."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Blueprint")),
				false,
				NAME_None);
			FormatGraphComboEntry.InsertPosition = FToolMenuInsert(TEXT("CompileBlueprint"), EToolMenuInsertType::After);
			Section.AddEntry(FormatGraphComboEntry);
		}
		{
			FToolMenuEntry FormatSelEntry = FToolMenuEntry::InitToolBarButton(
				TEXT("UnrealAiFormatBlueprintSelection"),
				FUIAction(
					FExecuteAction::CreateLambda([]()
					{
						UBlueprint* BP = nullptr;
						UEdGraph* Graph = nullptr;
						int32 Count = 0;
						if (!UnrealAiTryGetFocusedBlueprintUbergraphSelection(BP, Graph, Count) || !BP || !Graph || Count <= 0)
						{
							return;
						}
						TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
						J->SetStringField(TEXT("blueprint_path"), BP->GetPathName());
						J->SetStringField(TEXT("graph_name"), Graph->GetName());
						const FUnrealAiToolInvocationResult R = UnrealAiDispatch_BlueprintFormatSelection(J);
						if (!R.bOk)
						{
							UE_LOG(LogTemp, Warning, TEXT("Unreal AI format selection (ubergraph): %s"), *R.ErrorMessage);
						}
					}),
					FCanExecuteAction::CreateLambda([]()
					{
						UBlueprint* BP = nullptr;
						UEdGraph* Graph = nullptr;
						int32 Count = 0;
						return UnrealAiTryGetFocusedBlueprintUbergraphSelection(BP, Graph, Count) && Count > 0;
					})),
				LOCTEXT("FmtSelBtn", "AI: Format selection"),
				LOCTEXT("FmtSelTip", "Run bundled layout on the Blueprint Ubergraph selection. Uses Unreal AI Editor → Blueprint Formatting preferences."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GraphEditor.StateMachine_24x")));
			FormatSelEntry.InsertPosition = FToolMenuInsert(TEXT("UnrealAiFormatBlueprintUbergraph"), EToolMenuInsertType::After);
			Section.AddEntry(FormatSelEntry);
		}
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
			TEXT("Layout selected nodes. Args: blueprint_path [graph_name]. Uses Editor Preferences → Unreal AI Editor → Blueprint Formatting. Requires Blueprint editor open."),
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
