// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

namespace UnrealBlueprintStrandLayout
{
	/**
	 * Phase A strand layout: partition nodes into exec-flow components (undirected exec edges),
	 * topologically order within each component (Guid tie-break on cycles), lay out each strand
	 * horizontally with LaneGapY between lanes. Comment nodes should be excluded by the caller.
	 */
	void LayoutNodesMultiStrand(UEdGraph* Graph, const TArray<UEdGraphNode*>& Nodes, int32& OutNodesPositioned);
}
