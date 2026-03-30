#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/**
 * When the Blueprint editor is open, collect currently selected graph nodes.
 * @param GraphOrFocused If non-null, ensures that graph is the active document first; otherwise uses the editor focused graph.
 */
bool UnrealAiTryGetBlueprintGraphSelectedNodes(
	UBlueprint* BP,
	UEdGraph* GraphOrFocused,
	TArray<UEdGraphNode*>& OutSelected,
	FString* OutError);
