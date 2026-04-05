#include "BlueprintFormat/UnrealAiBlueprintGraphSelectionLayout.h"

#include "BlueprintFormat/UnrealAiBlueprintEditorGraphSelection.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorModule.h"

bool UnrealAiTryGetBlueprintGraphSelectedNodes(
	UBlueprint* BP,
	UEdGraph* GraphOrFocused,
	TArray<UEdGraphNode*>& OutSelected,
	FString* OutError,
	bool bBringTargetGraphToFront)
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

	if (bBringTargetGraphToFront)
	{
		Ed->OpenGraphAndBringToFront(TargetGraph, false);
	}

	UnrealAiAppendSelectedGraphNodesForGraph(Ed, TargetGraph, OutSelected);

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
