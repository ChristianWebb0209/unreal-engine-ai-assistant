// SPDX-License-Identifier: MIT

#include "BlueprintFormat/BlueprintGraphCommentReflow.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"

namespace UnrealBlueprintCommentReflowPriv
{
	struct FNodeRect
	{
		int32 MinX = 0;
		int32 MinY = 0;
		int32 MaxX = 0;
		int32 MaxY = 0;
	};

	static FNodeRect GetNodeRect(const UEdGraphNode* Node)
	{
		FNodeRect Rect;
		if (!Node)
		{
			return Rect;
		}
		const int32 SafeWidth = FMath::Max(64, Node->NodeWidth > 0 ? Node->NodeWidth : 240);
		const int32 SafeHeight = FMath::Max(32, Node->NodeHeight > 0 ? Node->NodeHeight : 120);
		Rect.MinX = Node->NodePosX;
		Rect.MinY = Node->NodePosY;
		Rect.MaxX = Node->NodePosX + SafeWidth;
		Rect.MaxY = Node->NodePosY + SafeHeight;
		return Rect;
	}

	static bool IsNodeCenterInsideRect(const UEdGraphNode* Node, const FNodeRect& Rect)
	{
		if (!Node)
		{
			return false;
		}
		const FNodeRect NodeRect = GetNodeRect(Node);
		const int32 CX = (NodeRect.MinX + NodeRect.MaxX) / 2;
		const int32 CY = (NodeRect.MinY + NodeRect.MaxY) / 2;
		return CX >= Rect.MinX && CX <= Rect.MaxX && CY >= Rect.MinY && CY <= Rect.MaxY;
	}

	static void FitCommentToMemberNodes(UEdGraphNode_Comment* Comment, const TArray<UEdGraphNode*>& Members, int32 Padding)
	{
		if (!Comment || Members.Num() == 0)
		{
			return;
		}
		FNodeRect Bounds;
		bool bAny = false;
		for (UEdGraphNode* Node : Members)
		{
			if (!Node)
			{
				continue;
			}
			const FNodeRect Rect = GetNodeRect(Node);
			if (!bAny)
			{
				Bounds = Rect;
				bAny = true;
			}
			else
			{
				Bounds.MinX = FMath::Min(Bounds.MinX, Rect.MinX);
				Bounds.MinY = FMath::Min(Bounds.MinY, Rect.MinY);
				Bounds.MaxX = FMath::Max(Bounds.MaxX, Rect.MaxX);
				Bounds.MaxY = FMath::Max(Bounds.MaxY, Rect.MaxY);
			}
		}
		if (!bAny)
		{
			return;
		}
		const int32 NewMinX = Bounds.MinX - Padding;
		const int32 NewMinY = Bounds.MinY - Padding;
		const int32 NewMaxX = Bounds.MaxX + Padding;
		const int32 NewMaxY = Bounds.MaxY + Padding;
		Comment->NodePosX = NewMinX;
		Comment->NodePosY = NewMinY;
		Comment->NodeWidth = FMath::Max(64, NewMaxX - NewMinX);
		Comment->NodeHeight = FMath::Max(64, NewMaxY - NewMinY);
	}
}

void UnrealBlueprintCommentReflow::FitCommentAroundNodes(
	UEdGraphNode_Comment* Comment,
	const TArray<UEdGraphNode*>& Members,
	int32 Padding)
{
	UnrealBlueprintCommentReflowPriv::FitCommentToMemberNodes(Comment, Members, Padding);
}

int32 UnrealBlueprintCommentReflow::RefitAllCommentsToGeometricMembers(UEdGraph* Graph, int32 Padding)
{
	using namespace UnrealBlueprintCommentReflowPriv;
	if (!Graph)
	{
		return 0;
	}
	TArray<UEdGraphNode_Comment*> Comments;
	TArray<UEdGraphNode*> NonComments;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N)
		{
			continue;
		}
		if (UEdGraphNode_Comment* C = Cast<UEdGraphNode_Comment>(N))
		{
			Comments.Add(C);
		}
		else
		{
			NonComments.Add(N);
		}
	}
	int32 Adjusted = 0;
	for (UEdGraphNode_Comment* Comment : Comments)
	{
		if (!Comment)
		{
			continue;
		}
		const int32 PX = Comment->NodePosX;
		const int32 PY = Comment->NodePosY;
		const int32 PW = Comment->NodeWidth;
		const int32 PH = Comment->NodeHeight;
		const FNodeRect CommentRect = GetNodeRect(Comment);
		TArray<UEdGraphNode*> Members;
		for (UEdGraphNode* Node : NonComments)
		{
			if (IsNodeCenterInsideRect(Node, CommentRect))
			{
				Members.Add(Node);
			}
		}
		FitCommentToMemberNodes(Comment, Members, Padding);
		if (Comment->NodePosX != PX || Comment->NodePosY != PY || Comment->NodeWidth != PW
			|| Comment->NodeHeight != PH)
		{
			++Adjusted;
		}
	}
	return Adjusted;
}
