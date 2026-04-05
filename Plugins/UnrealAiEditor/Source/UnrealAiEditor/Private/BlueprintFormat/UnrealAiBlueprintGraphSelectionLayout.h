#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/**
 * When the Blueprint editor is open, collect currently selected graph nodes.
 * @param GraphOrFocused If non-null, targets that graph; otherwise uses the editor focused graph.
 * @param bBringTargetGraphToFront When true (default), activates the graph tab in the Blueprint editor (steals focus). When false, leaves tab order unchanged (respects Editor focus off).
 */
bool UnrealAiTryGetBlueprintGraphSelectedNodes(
	UBlueprint* BP,
	UEdGraph* GraphOrFocused,
	TArray<UEdGraphNode*>& OutSelected,
	FString* OutError,
	bool bBringTargetGraphToFront = true);
