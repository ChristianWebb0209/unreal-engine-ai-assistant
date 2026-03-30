// SPDX-License-Identifier: MIT
// Derived from Unreal Blueprint Formatter (https://github.com/ChristianWebb0209/ue-blueprint-formatter).

#include "BlueprintFormat/BlueprintGraphFormatService.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"

namespace UnrealBlueprintFormatServicePriv
{
	struct FNodeRect
	{
		int32 MinX = 0;
		int32 MinY = 0;
		int32 MaxX = 0;
		int32 MaxY = 0;
	};

	static FNodeRect GetNodeRect(
		const UEdGraphNode* Node,
		const TMap<const UEdGraphNode*, FIntPoint>* PositionOverride = nullptr)
	{
		FNodeRect Rect;
		if (!Node)
		{
			return Rect;
		}

		// Some nodes do not have dimensions populated until they have been drawn.
		const int32 SafeWidth = FMath::Max(64, Node->NodeWidth > 0 ? Node->NodeWidth : 240);
		const int32 SafeHeight = FMath::Max(32, Node->NodeHeight > 0 ? Node->NodeHeight : 120);

		int32 PosX = Node->NodePosX;
		int32 PosY = Node->NodePosY;
		if (PositionOverride)
		{
			if (const FIntPoint* Override = PositionOverride->Find(Node))
			{
				PosX = Override->X;
				PosY = Override->Y;
			}
		}

		Rect.MinX = PosX;
		Rect.MinY = PosY;
		Rect.MaxX = PosX + SafeWidth;
		Rect.MaxY = PosY + SafeHeight;
		return Rect;
	}

	static bool IsNodeCenterInsideRect(
		const UEdGraphNode* Node,
		const FNodeRect& Rect,
		const TMap<const UEdGraphNode*, FIntPoint>* PositionOverride = nullptr)
	{
		if (!Node)
		{
			return false;
		}
		const FNodeRect NodeRect = GetNodeRect(Node, PositionOverride);
		const int32 CX = (NodeRect.MinX + NodeRect.MaxX) / 2;
		const int32 CY = (NodeRect.MinY + NodeRect.MaxY) / 2;
		return CX >= Rect.MinX && CX <= Rect.MaxX && CY >= Rect.MinY && CY <= Rect.MaxY;
	}

	static TArray<UEdGraphNode_Comment*> GetComments(UEdGraph* Graph)
	{
		TArray<UEdGraphNode_Comment*> Comments;
		if (!Graph)
		{
			return Comments;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
			{
				Comments.Add(Comment);
			}
		}
		return Comments;
	}

	static TMap<const UEdGraphNode*, FIntPoint> SnapshotNodePositions(const TArray<UEdGraphNode*>& Nodes)
	{
		TMap<const UEdGraphNode*, FIntPoint> Out;
		for (UEdGraphNode* Node : Nodes)
		{
			if (Node)
			{
				Out.Add(Node, FIntPoint(Node->NodePosX, Node->NodePosY));
			}
		}
		return Out;
	}

	static TMap<UEdGraphNode_Comment*, TArray<UEdGraphNode*>> BuildInitialCommentMembers(
		const TArray<UEdGraphNode*>& Nodes,
		const TArray<UEdGraphNode_Comment*>& Comments,
		const TMap<const UEdGraphNode*, FIntPoint>& InitialPositions)
	{
		TMap<UEdGraphNode_Comment*, TArray<UEdGraphNode*>> Members;
		for (UEdGraphNode_Comment* Comment : Comments)
		{
			if (Comment)
			{
				Members.Add(Comment, {});
			}
		}

		for (UEdGraphNode* Node : Nodes)
		{
			if (!Node || Cast<UEdGraphNode_Comment>(Node))
			{
				continue;
			}
			for (UEdGraphNode_Comment* Comment : Comments)
			{
				if (!Comment)
				{
					continue;
				}
				const FNodeRect CommentRect = GetNodeRect(Comment, &InitialPositions);
				if (IsNodeCenterInsideRect(Node, CommentRect, &InitialPositions))
				{
					Members.FindOrAdd(Comment).Add(Node);
				}
			}
		}

		return Members;
	}

	static TMap<UEdGraphNode*, TSet<UEdGraphNode_Comment*>> InvertMembership(
		const TMap<UEdGraphNode_Comment*, TArray<UEdGraphNode*>>& CommentMembers)
	{
		TMap<UEdGraphNode*, TSet<UEdGraphNode_Comment*>> Out;
		for (const TPair<UEdGraphNode_Comment*, TArray<UEdGraphNode*>>& Pair : CommentMembers)
		{
			UEdGraphNode_Comment* Comment = Pair.Key;
			for (UEdGraphNode* Node : Pair.Value)
			{
				if (Node && Comment)
				{
					Out.FindOrAdd(Node).Add(Comment);
				}
			}
		}
		return Out;
	}

	static void FitCommentToMemberNodes(
		UEdGraphNode_Comment* Comment,
		const TArray<UEdGraphNode*>& Members,
		int32 Padding)
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

	static bool HasMovedSinceSnapshot(
		const UEdGraphNode* Node,
		const TMap<const UEdGraphNode*, FIntPoint>& BeforePositions)
	{
		if (!Node)
		{
			return false;
		}
		const FIntPoint* Before = BeforePositions.Find(Node);
		if (!Before)
		{
			return false;
		}
		return Before->X != Node->NodePosX || Before->Y != Node->NodePosY;
	}

	static void TranslateCommentGroup(
		UEdGraphNode_Comment* Comment,
		const TArray<UEdGraphNode*>& Members,
		int32 DX,
		int32 DY)
	{
		if (!Comment || (DX == 0 && DY == 0))
		{
			return;
		}

		Comment->NodePosX += DX;
		Comment->NodePosY += DY;
		for (UEdGraphNode* Node : Members)
		{
			if (!Node)
			{
				continue;
			}
			Node->NodePosX += DX;
			Node->NodePosY += DY;
		}
	}

	static void EnforceCommentContainmentAfterMove(
		UEdGraph* Graph,
		const TArray<UEdGraphNode*>& MovedCandidates,
		const TMap<const UEdGraphNode*, FIntPoint>& BeforePositions)
	{
		constexpr int32 CommentPadding = 80;
		constexpr int32 PushGap = 64;

		const TArray<UEdGraphNode_Comment*> Comments = GetComments(Graph);
		if (Comments.Num() == 0)
		{
			return;
		}

		TArray<UEdGraphNode*> AllNodes;
		AllNodes.Reserve(Graph->Nodes.Num());
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node)
			{
				AllNodes.Add(Node);
			}
		}

		TMap<UEdGraphNode_Comment*, TArray<UEdGraphNode*>> InitialMembers =
			BuildInitialCommentMembers(AllNodes, Comments, BeforePositions);
		const TMap<UEdGraphNode*, TSet<UEdGraphNode_Comment*>> NodeInitialMembership = InvertMembership(InitialMembers);

		// First, preserve existing memberships by fitting each comment to its original member nodes.
		for (UEdGraphNode_Comment* Comment : Comments)
		{
			FitCommentToMemberNodes(Comment, InitialMembers.FindOrAdd(Comment), CommentPadding);
		}

		// If a moved node would get captured by a comment it did not previously belong to,
		// translate that entire comment group away from the moved node.
		for (UEdGraphNode* Node : MovedCandidates)
		{
			if (!Node || Cast<UEdGraphNode_Comment>(Node) || !HasMovedSinceSnapshot(Node, BeforePositions))
			{
				continue;
			}

			const TSet<UEdGraphNode_Comment*>* OriginalSet = NodeInitialMembership.Find(Node);
			for (UEdGraphNode_Comment* Comment : Comments)
			{
				if (!Comment)
				{
					continue;
				}

				if (OriginalSet && OriginalSet->Contains(Comment))
				{
					continue;
				}

				const FNodeRect CommentRect = GetNodeRect(Comment);
				if (!IsNodeCenterInsideRect(Node, CommentRect))
				{
					continue;
				}

				const FNodeRect NodeRect = GetNodeRect(Node);
				const int32 NodeCenterX = (NodeRect.MinX + NodeRect.MaxX) / 2;
				const int32 CommentCenterX = (CommentRect.MinX + CommentRect.MaxX) / 2;
				const bool bPushRight = NodeCenterX <= CommentCenterX;
				const int32 Shift = bPushRight
					? (NodeRect.MaxX - CommentRect.MinX + PushGap)
					: (CommentRect.MaxX - NodeRect.MinX + PushGap);

				const int32 DX = bPushRight ? Shift : -Shift;
				TranslateCommentGroup(Comment, InitialMembers.FindOrAdd(Comment), DX, 0);
			}
		}

		// Re-fit after any group translation to keep each comment tightly containing its members.
		for (UEdGraphNode_Comment* Comment : Comments)
		{
			FitCommentToMemberNodes(Comment, InitialMembers.FindOrAdd(Comment), CommentPadding);
		}
	}

	static void LayoutNodesHorizontal(TArray<UEdGraphNode*>& Nodes, FUnrealBlueprintGraphFormatResult& OutResult)
	{
		Nodes.Sort([](const UEdGraphNode& A, const UEdGraphNode& B) { return A.NodeGuid < B.NodeGuid; });
		int32 X = 0;
		const int32 DX = 400;
		for (UEdGraphNode* N : Nodes)
		{
			if (!N)
			{
				continue;
			}
			N->NodePosX = X;
			N->NodePosY = 0;
			X += DX;
			++OutResult.NodesPositioned;
		}
	}
}

FUnrealBlueprintGraphFormatResult FUnrealBlueprintGraphFormatService::LayoutAfterAiIrApply(
	UEdGraph* Graph,
	const TArray<UEdGraphNode*>& MaterializedNodes,
	const TArray<FUnrealBlueprintIrNodeLayoutHint>& Hints)
{
	FUnrealBlueprintGraphFormatResult R;
	if (!Graph || MaterializedNodes.Num() == 0)
	{
		return R;
	}

	const TMap<const UEdGraphNode*, FIntPoint> BeforePositions =
		UnrealBlueprintFormatServicePriv::SnapshotNodePositions(MaterializedNodes);

	bool bAllZeroHints = true;
	for (const FUnrealBlueprintIrNodeLayoutHint& H : Hints)
	{
		if (H.X != 0 || H.Y != 0)
		{
			bAllZeroHints = false;
			break;
		}
	}

	if (!bAllZeroHints && Hints.Num() == MaterializedNodes.Num())
	{
		for (int32 i = 0; i < MaterializedNodes.Num(); ++i)
		{
			UEdGraphNode* N = MaterializedNodes[i];
			if (!N)
			{
				continue;
			}
			N->NodePosX = Hints[i].X;
			N->NodePosY = Hints[i].Y;
			++R.NodesPositioned;
		}
		UnrealBlueprintFormatServicePriv::EnforceCommentContainmentAfterMove(Graph, MaterializedNodes, BeforePositions);
		return R;
	}

	TArray<UEdGraphNode*> Copy = MaterializedNodes;
	UnrealBlueprintFormatServicePriv::LayoutNodesHorizontal(Copy, R);
	UnrealBlueprintFormatServicePriv::EnforceCommentContainmentAfterMove(Graph, MaterializedNodes, BeforePositions);
	return R;
}

void FUnrealBlueprintGraphFormatService::LayoutEntireGraph(UEdGraph* Graph)
{
	if (!Graph)
	{
		return;
	}
	TArray<UEdGraphNode*> Nodes;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (N)
		{
			Nodes.Add(N);
		}
	}
	const TMap<const UEdGraphNode*, FIntPoint> BeforePositions =
		UnrealBlueprintFormatServicePriv::SnapshotNodePositions(Nodes);
	FUnrealBlueprintGraphFormatResult Tmp;
	UnrealBlueprintFormatServicePriv::LayoutNodesHorizontal(Nodes, Tmp);
	UnrealBlueprintFormatServicePriv::EnforceCommentContainmentAfterMove(Graph, Nodes, BeforePositions);
}

void FUnrealBlueprintGraphFormatService::LayoutSelectedNodes(UEdGraph* Graph, const TArray<UEdGraphNode*>& SelectedNodes)
{
	if (!Graph || SelectedNodes.Num() == 0)
	{
		return;
	}

	TArray<UEdGraphNode*> Nodes;
	Nodes.Reserve(SelectedNodes.Num());
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (Node)
		{
			Nodes.Add(Node);
		}
	}

	if (Nodes.Num() == 0)
	{
		return;
	}

	const TMap<const UEdGraphNode*, FIntPoint> BeforePositions =
		UnrealBlueprintFormatServicePriv::SnapshotNodePositions(Nodes);
	FUnrealBlueprintGraphFormatResult Tmp;
	UnrealBlueprintFormatServicePriv::LayoutNodesHorizontal(Nodes, Tmp);
	UnrealBlueprintFormatServicePriv::EnforceCommentContainmentAfterMove(Graph, Nodes, BeforePositions);
}
