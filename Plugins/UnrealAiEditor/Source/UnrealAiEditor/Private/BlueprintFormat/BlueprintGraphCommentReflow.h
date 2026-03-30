// SPDX-License-Identifier: MIT
#pragma once

class UEdGraph;
class UEdGraphNode;
class UEdGraphNode_Comment;

namespace UnrealBlueprintCommentReflow
{
	/**
	 * Recompute membership by “node center inside comment bounds” and resize each comment
	 * to tightly fit its members (padding in graph units).
	 */
	void RefitAllCommentsToGeometricMembers(UEdGraph* Graph, int32 Padding = 80);

	/** Resize a comment to bound the given member nodes (IR `member_node_ids` after layout). */
	void FitCommentAroundNodes(UEdGraphNode_Comment* Comment, const TArray<UEdGraphNode*>& Members, int32 Padding = 80);
}
