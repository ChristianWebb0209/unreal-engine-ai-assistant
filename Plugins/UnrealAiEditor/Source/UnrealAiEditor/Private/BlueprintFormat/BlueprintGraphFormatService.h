// SPDX-License-Identifier: MIT
// Derived from Unreal Blueprint Formatter (https://github.com/ChristianWebb0209/ue-blueprint-formatter).
// Vendored into Unreal AI Editor under Plugins/UnrealAiEditor/Source/.../Private/BlueprintFormat/.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

/** Per-node layout hint from Unreal AI blueprint_apply_ir (matches IR node_id + x/y). */
struct FUnrealBlueprintIrNodeLayoutHint
{
	FString NodeId;
	int32 X = 0;
	int32 Y = 0;
};

/** Result of a layout pass (warnings + how many nodes received new positions). */
struct FUnrealBlueprintGraphFormatResult
{
	TArray<FString> Warnings;
	int32 NodesPositioned = 0;
};

/**
 * Exec-flow–aware graph layout for Blueprint editor graphs.
 */
struct FUnrealBlueprintGraphFormatService
{
	/** Layout only the materialized IR nodes (typically after apply); respects hints when not all-zero. */
	static FUnrealBlueprintGraphFormatResult LayoutAfterAiIrApply(
		UEdGraph* Graph,
		const TArray<UEdGraphNode*>& MaterializedNodes,
		const TArray<FUnrealBlueprintIrNodeLayoutHint>& Hints);

	/** Layout every node in the graph. */
	static void LayoutEntireGraph(UEdGraph* Graph);

	/** Layout only the provided nodes in the graph. */
	static void LayoutSelectedNodes(UEdGraph* Graph, const TArray<UEdGraphNode*>& SelectedNodes);
};
