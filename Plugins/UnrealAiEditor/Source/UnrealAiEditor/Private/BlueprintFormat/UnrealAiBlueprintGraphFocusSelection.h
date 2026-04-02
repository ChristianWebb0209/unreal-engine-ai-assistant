#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;

/**
 * Read-only helper for editor UI:
 * - Detects whether the currently focused Blueprint graph is the Blueprint Ubergraph (Event Graph).
 * - Counts selected nodes in that exact focused graph.
 *
 * This must not open/bring graph tabs to front (safe for CanExecute).
 */
bool UnrealAiTryGetFocusedBlueprintUbergraphSelection(
	UBlueprint*& OutBlueprint,
	UEdGraph*& OutFocusedUbergraph,
	int32& OutSelectedNodeCount);

