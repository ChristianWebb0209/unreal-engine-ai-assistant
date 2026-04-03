#pragma once

#include "CoreMinimal.h"

class IBlueprintEditor;
class UEdGraph;
class UEdGraphNode;

/** Graph node selection from the active Blueprint editor (not GEditor object selection). */
void UnrealAiAppendSelectedGraphNodesForGraph(
	const TSharedPtr<IBlueprintEditor>& Ed,
	UEdGraph* TargetGraph,
	TArray<UEdGraphNode*>& OutNodes);
