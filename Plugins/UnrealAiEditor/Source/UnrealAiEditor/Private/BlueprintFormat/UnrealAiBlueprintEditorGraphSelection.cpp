#include "BlueprintFormat/UnrealAiBlueprintEditorGraphSelection.h"

#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

void UnrealAiAppendSelectedGraphNodesForGraph(
	const TSharedPtr<IBlueprintEditor>& Ed,
	UEdGraph* TargetGraph,
	TArray<UEdGraphNode*>& OutNodes)
{
	if (!Ed.IsValid() || !TargetGraph)
	{
		return;
	}

	const TSharedPtr<FBlueprintEditor> BpEditor = StaticCastSharedPtr<FBlueprintEditor>(Ed);
	if (!BpEditor.IsValid())
	{
		return;
	}

	const FGraphPanelSelectionSet Selected = BpEditor->GetSelectedNodes();
	for (UObject* Obj : Selected)
	{
		UEdGraphNode* const GN = Cast<UEdGraphNode>(Obj);
		if (GN && GN->GetGraph() == TargetGraph)
		{
			OutNodes.Add(GN);
		}
	}
}
