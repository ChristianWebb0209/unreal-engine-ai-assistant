#include "BlueprintFormat/UnrealAiBlueprintGraphSelectionLayout.h"

#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorModule.h"
#include "Selection.h"

bool UnrealAiTryGetBlueprintGraphSelectedNodes(
	UBlueprint* BP,
	UEdGraph* GraphOrFocused,
	TArray<UEdGraphNode*>& OutSelected,
	FString* OutError)
{
	OutSelected.Reset();
	if (!BP)
	{
		if (OutError)
		{
			*OutError = TEXT("Blueprint is null.");
		}
		return false;
	}

	const TSharedPtr<IBlueprintEditor> Ed = FKismetEditorUtilities::GetIBlueprintEditorForObject(BP, false);
	if (!Ed.IsValid())
	{
		if (OutError)
		{
			*OutError = TEXT("Open this Blueprint in the editor to format the current graph selection.");
		}
		return false;
	}

	UEdGraph* TargetGraph = GraphOrFocused;
	if (!TargetGraph)
	{
		TargetGraph = Ed->GetFocusedGraph();
	}
	if (!TargetGraph)
	{
		if (OutError)
		{
			*OutError = TEXT("No focused Blueprint graph.");
		}
		return false;
	}

	Ed->OpenGraphAndBringToFront(TargetGraph, false);

	if (GEditor)
	{
		if (USelection* ObjSel = GEditor->GetSelectedObjects())
		{
			for (int32 i = 0; i < ObjSel->Num(); ++i)
			{
				if (UEdGraphNode* GN = Cast<UEdGraphNode>(ObjSel->GetSelectedObject(i)))
				{
					if (GN->GetGraph() == TargetGraph)
					{
						OutSelected.Add(GN);
					}
				}
			}
		}
	}

	if (OutSelected.Num() == 0)
	{
		if (OutError)
		{
			*OutError = TEXT("No nodes selected in the target graph (select nodes in the graph, then retry).");
		}
		return false;
	}

	return true;
}
