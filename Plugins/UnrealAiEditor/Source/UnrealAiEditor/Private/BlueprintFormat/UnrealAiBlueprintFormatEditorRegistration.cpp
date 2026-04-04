#include "BlueprintFormat/UnrealAiBlueprintFormatEditorRegistration.h"

#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/IConsoleManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "ToolMenuEntry.h"
#include "UnrealAiEditorSettings.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

#include "BlueprintFormat/UnrealAiBlueprintGraphFocusSelection.h"

#define LOCTEXT_NAMESPACE "UnrealAiBlueprintFormatEditorRegistration"

namespace UnrealAiBlueprintFormatEditorRegistrationPriv
{
	static IConsoleObject* GFormatSelectionCmd = nullptr;

	static FText SpacingDensityLabel(const UUnrealAiEditorSettings* S)
	{
		if (!S)
		{
			return LOCTEXT("FmtOptsCombo", "AI: Format options");
		}
		switch (S->BlueprintFormatSpacingDensity)
		{
		case EUnrealAiBlueprintFormatSpacingDensity::Sparse:
			return LOCTEXT("FmtOptsSparse", "AI: Sparse");
		case EUnrealAiBlueprintFormatSpacingDensity::Dense:
			return LOCTEXT("FmtOptsDense", "AI: Dense");
		default:
			return LOCTEXT("FmtOptsMedium", "AI: Medium");
		}
	}

	static TSharedRef<SWidget> BuildFormatOptionsMenu()
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("FmtOptsHeading", "Blueprint formatting"));
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("SpacingMenu", "Spacing density"),
				LOCTEXT("SpacingMenuTip", "Column/row spacing for the bundled formatter (Editor Preferences)"),
				FNewMenuDelegate::CreateLambda([](FMenuBuilder& Sub)
				{
					auto Set = [](EUnrealAiBlueprintFormatSpacingDensity D)
					{
						UUnrealAiEditorSettings* St = GetMutableDefault<UUnrealAiEditorSettings>();
						St->BlueprintFormatSpacingDensity = D;
						St->SaveConfig();
					};
					Sub.AddMenuEntry(
						LOCTEXT("Sparse", "Sparse"),
						LOCTEXT("SparseTip", "Wider grid"),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Set]() { Set(EUnrealAiBlueprintFormatSpacingDensity::Sparse); })));
					Sub.AddMenuEntry(
						LOCTEXT("Medium", "Medium"),
						LOCTEXT("MediumTip", "Default grid"),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Set]() { Set(EUnrealAiBlueprintFormatSpacingDensity::Medium); })));
					Sub.AddMenuEntry(
						LOCTEXT("Dense", "Dense"),
						LOCTEXT("DenseTip", "Tighter grid"),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Set]() { Set(EUnrealAiBlueprintFormatSpacingDensity::Dense); })));
				}));
			MenuBuilder.AddSubMenu(
				LOCTEXT("CommentsMenu", "Comment synthesis"),
				LOCTEXT("CommentsMenuTip", "Auto region comments when the formatter adds boxes"),
				FNewMenuDelegate::CreateLambda([](FMenuBuilder& Sub)
				{
					auto Set = [](EUnrealAiBlueprintCommentsMode M)
					{
						UUnrealAiEditorSettings* St = GetMutableDefault<UUnrealAiEditorSettings>();
						St->BlueprintCommentsMode = M;
						St->SaveConfig();
					};
					Sub.AddMenuEntry(
						LOCTEXT("CommOff", "Off"),
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Set]() { Set(EUnrealAiBlueprintCommentsMode::Off); })));
					Sub.AddMenuEntry(
						LOCTEXT("CommMin", "Minimal"),
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Set]() { Set(EUnrealAiBlueprintCommentsMode::Minimal); })));
					Sub.AddMenuEntry(
						LOCTEXT("CommVerb", "Verbose"),
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Set]() { Set(EUnrealAiBlueprintCommentsMode::Verbose); })));
				}));
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ToggleKnots", "Toggle wire knots"),
				LOCTEXT("ToggleKnotsTip", "Reroute knots on long data wires (best-effort)"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					UUnrealAiEditorSettings* St = GetMutableDefault<UUnrealAiEditorSettings>();
					St->bBlueprintFormatUseWireKnots = !St->bBlueprintFormatUseWireKnots;
					St->SaveConfig();
				})));
			MenuBuilder.AddMenuEntry(
				LOCTEXT("TogglePreserve", "Toggle preserve existing positions"),
				LOCTEXT("TogglePreserveTip", "Skip repositioning nodes that already have non-zero coordinates"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					UUnrealAiEditorSettings* St = GetMutableDefault<UUnrealAiEditorSettings>();
					St->bBlueprintFormatPreserveExistingPositions = !St->bBlueprintFormatPreserveExistingPositions;
					St->SaveConfig();
				})));
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ToggleReflow", "Toggle reflow comment boxes"),
				LOCTEXT("ToggleReflowTip", "Resize comment nodes to fit members after layout"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					UUnrealAiEditorSettings* St = GetMutableDefault<UUnrealAiEditorSettings>();
					St->bBlueprintFormatReflowCommentsByGeometry = !St->bBlueprintFormatReflowCommentsByGeometry;
					St->SaveConfig();
				})));
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
			FToolMenuEntry FormatGraphEntry = FToolMenuEntry::InitToolBarButton(
				TEXT("UnrealAiFormatBlueprintUbergraph"),
				FUIAction(
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
					})),
				LOCTEXT("FmtBlueprintBtn", "AI: Format graph"),
				LOCTEXT("FmtBlueprintTip", "Run bundled layout on the Blueprint Ubergraph (Event Graph). Uses Unreal AI Editor → Blueprint Formatting preferences."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Blueprint")));
			FormatGraphEntry.InsertPosition = FToolMenuInsert(TEXT("CompileBlueprint"), EToolMenuInsertType::After);
			Section.AddEntry(FormatGraphEntry);
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
		{
			const TSharedRef<SWidget> ComboWidget = SNew(SComboButton)
				.OnGetMenuContent_Lambda([]()
				{
					return UnrealAiBlueprintFormatEditorRegistrationPriv::BuildFormatOptionsMenu();
				})
				.ToolTipText(LOCTEXT("FmtOptsComboTip", "Change Blueprint formatter options (same as Editor Preferences → Plugins → Unreal AI Editor)."))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([]()
					{
						return UnrealAiBlueprintFormatEditorRegistrationPriv::SpacingDensityLabel(
							GetDefault<UUnrealAiEditorSettings>());
					})
				];
			FToolMenuEntry FmtOptsEntry = FToolMenuEntry::InitWidget(
				TEXT("UnrealAiBlueprintFormatOptionsCombo"),
				ComboWidget,
				LOCTEXT("FmtOptsLabel", "AI format"),
				false,
				true,
				false,
				LOCTEXT("FmtOptsComboTip", "Change Blueprint formatter options (same as Editor Preferences → Plugins → Unreal AI Editor)."));
			FmtOptsEntry.InsertPosition = FToolMenuInsert(TEXT("UnrealAiFormatBlueprintSelection"), EToolMenuInsertType::After);
			Section.AddEntry(FmtOptsEntry);
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
