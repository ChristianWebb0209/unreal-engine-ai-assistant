// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

namespace UnrealBlueprintStrandLayout
{
	/**
	 * Exec-flow layout: isolates exec components, depth layers on exec edges, branches fan up/down by
	 * pin order, column packing. Caller applies a global script-node overlap pass afterward.
	 */
	void LayoutNodesMultiStrand(UEdGraph* Graph, const TArray<UEdGraphNode*>& Nodes, int32& OutNodesPositioned);
}
