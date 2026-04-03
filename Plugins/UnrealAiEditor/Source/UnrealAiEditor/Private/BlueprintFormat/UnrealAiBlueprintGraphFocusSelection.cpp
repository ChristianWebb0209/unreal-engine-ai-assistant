#include "BlueprintFormat/UnrealAiBlueprintGraphFocusSelection.h"

#include "BlueprintFormat/UnrealAiBlueprintEditorGraphSelection.h"

#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"

bool UnrealAiTryGetFocusedBlueprintUbergraphSelection(
	UBlueprint*& OutBlueprint,
	UEdGraph*& OutFocusedUbergraph,
	int32& OutSelectedNodeCount)
{
	OutBlueprint = nullptr;
	OutFocusedUbergraph = nullptr;
	OutSelectedNodeCount = 0;

	if (!GEditor)
	{
		return false;
	}

	UAssetEditorSubsystem* Subsys = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!Subsys)
	{
		return false;
	}

	const TArray<UObject*> EditedAssets = Subsys->GetAllEditedAssets();
	for (UObject* A : EditedAssets)
	{
		UBlueprint* BP = Cast<UBlueprint>(A);
		if (!BP)
		{
			continue;
		}

		const TSharedPtr<IBlueprintEditor> Ed = FKismetEditorUtilities::GetIBlueprintEditorForObject(BP, false);
		if (!Ed.IsValid())
		{
			continue;
		}

		UEdGraph* FocusedGraph = Ed->GetFocusedGraph();
		if (!FocusedGraph)
		{
			continue;
		}

		// Focused graph must be exactly one of the Ubergraph pages for this BP.
		bool bIsUbergraph = false;
		for (const TObjectPtr<UEdGraph>& Uber : BP->UbergraphPages)
		{
			if (Uber.Get() == FocusedGraph)
			{
				bIsUbergraph = true;
				break;
			}
		}

		if (!bIsUbergraph)
		{
			continue;
		}

		OutBlueprint = BP;
		OutFocusedUbergraph = FocusedGraph;
		TArray<UEdGraphNode*> SelectedInGraph;
		UnrealAiAppendSelectedGraphNodesForGraph(Ed, FocusedGraph, SelectedInGraph);
		OutSelectedNodeCount = SelectedInGraph.Num();

		return true;
	}

	return false;
}

