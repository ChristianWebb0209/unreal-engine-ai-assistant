#include "BlueprintFormat/UnrealAiBlueprintGraphFocusSelection.h"

#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorModule.h"
#include "Selection.h"
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

	// Selection is global; we count only nodes selected in the focused ubergraph.
	USelection* SelectedObjects = GEditor->GetSelectedObjects();

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
		OutSelectedNodeCount = 0;
		if (SelectedObjects)
		{
			for (int32 i = 0; i < SelectedObjects->Num(); ++i)
			{
				if (UEdGraphNode* GN = Cast<UEdGraphNode>(SelectedObjects->GetSelectedObject(i)))
				{
					if (GN->GetGraph() == FocusedGraph)
					{
						++OutSelectedNodeCount;
					}
				}
			}
		}

		return true;
	}

	return false;
}

