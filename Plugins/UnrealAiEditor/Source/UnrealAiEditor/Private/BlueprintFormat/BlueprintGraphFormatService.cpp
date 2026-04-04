// SPDX-License-Identifier: MIT
// Derived from Unreal Blueprint Formatter (https://github.com/ChristianWebb0209/ue-blueprint-formatter).

#include "BlueprintFormat/BlueprintGraphFormatService.h"

#include "BlueprintFormat/BlueprintGraphAutoComments.h"
#include "BlueprintFormat/BlueprintGraphCommentReflow.h"
#include "BlueprintFormat/BlueprintGraphKnotService.h"
#include "BlueprintFormat/BlueprintGraphLayeredDagLayout.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"

namespace UnrealBlueprintFormatServicePriv
{
	static bool TakesPartInScriptLayout(const UEdGraphNode* N)
	{
		return N && !Cast<UEdGraphNode_Comment>(N);
	}

	static void RunPostLayoutPasses(
		UEdGraph* Graph,
		const FUnrealBlueprintGraphFormatOptions& Options,
		FUnrealBlueprintGraphFormatResult& R)
	{
		if (!Graph)
		{
			return;
		}
		if (Options.CommentsMode != EUnrealAiBlueprintCommentsMode::Off)
		{
			R.CommentsAdjusted += UnrealBlueprintAutoComments::MaybeAddRegionCommentsForLargeGraphs(Graph, Options);
		}
		{
			const int32 K = UnrealBlueprintKnotService::ApplyWireKnots(Graph, Options);
			R.KnotsInserted += K;
			if (K > 0)
			{
				R.Warnings.Add(FString::Printf(TEXT("inserted_data_knots:%d"), K));
			}
		}
		if (Options.bReflowCommentsByGeometry)
		{
			R.CommentsAdjusted += UnrealBlueprintCommentReflow::RefitAllCommentsToGeometricMembers(Graph);
		}
	}

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

	static bool NodeRectsOverlapWithGap(const FNodeRect& A, const FNodeRect& B, int32 Gap)
	{
		return !(A.MaxX + Gap <= B.MinX || B.MaxX + Gap <= A.MinX || A.MaxY + Gap <= B.MinY || B.MaxY + Gap <= A.MinY);
	}

	/** Minimum clear space between two nodes' boxes scales with their size (not a single hardcoded pixel value). */
	static int32 PairwiseSeparationGap(const UEdGraphNode* A, const UEdGraphNode* B)
	{
		const int32 Wa = FMath::Max(64, A && A->NodeWidth > 0 ? A->NodeWidth : 240);
		const int32 Wb = FMath::Max(64, B && B->NodeWidth > 0 ? B->NodeWidth : 240);
		const int32 Ha = FMath::Max(32, A && A->NodeHeight > 0 ? A->NodeHeight : 120);
		const int32 Hb = FMath::Max(32, B && B->NodeHeight > 0 ? B->NodeHeight : 120);
		return FMath::Clamp((Wa + Wb + Ha + Hb) / 16, 28, 120);
	}

	static int32 HorizontalGapBetweenConsecutive(const UEdGraphNode* Left, const UEdGraphNode* Right)
	{
		return PairwiseSeparationGap(Left, Right);
	}

	/** Rule 1: script nodes we position must not overlap (AABB + pairwise minimum gap). */
	static void ResolveAxisAlignedOverlapsAmong(
		TArray<UEdGraphNode*>& Nodes,
		const TSet<UEdGraphNode*>* LockedNodes = nullptr)
	{
		if (Nodes.Num() < 2)
		{
			return;
		}
		constexpr int32 MaxPasses = 128;
		Nodes.Sort([](UEdGraphNode& A, UEdGraphNode& B) { return A.NodeGuid < B.NodeGuid; });
		for (int32 Pass = 0; Pass < MaxPasses; ++Pass)
		{
			bool bMoved = false;
			for (int32 i = 0; i < Nodes.Num(); ++i)
			{
				UEdGraphNode* const A = Nodes[i];
				if (!A)
				{
					continue;
				}
				const FNodeRect Ra = GetNodeRect(A);
				for (int32 j = i + 1; j < Nodes.Num(); ++j)
				{
					UEdGraphNode* const B = Nodes[j];
					if (!B)
					{
						continue;
					}
					UEdGraphNode* const Mover = B;
					if (LockedNodes && LockedNodes->Contains(Mover))
					{
						continue;
					}
					const int32 G = PairwiseSeparationGap(A, B);
					const FNodeRect Rb = GetNodeRect(B);
					if (!NodeRectsOverlapWithGap(Ra, Rb, G))
					{
						continue;
					}
					const FNodeRect Ra2 = GetNodeRect(A);
					const FNodeRect Rm = GetNodeRect(Mover);
					int32 Dx = (Ra2.MaxX + G) - Rm.MinX;
					int32 Dy = (Ra2.MaxY + G) - Rm.MinY;
					if (Dx < 0)
					{
						Dx = 0;
					}
					if (Dy < 0)
					{
						Dy = 0;
					}
					const int32 Nudge = FMath::Max(8, G / 4);
					if (Dx <= Dy)
					{
						Mover->NodePosX += FMath::Max(Dx, Nudge);
					}
					else
					{
						Mover->NodePosY += FMath::Max(Dy, Nudge);
					}
					bMoved = true;
				}
			}
			if (!bMoved)
			{
				break;
			}
		}
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

	static void BuildPreserveLockedSet(const TArray<UEdGraphNode*>& ScriptNodes, TSet<UEdGraphNode*>& OutLocked)
	{
		OutLocked.Reset();
		for (UEdGraphNode* N : ScriptNodes)
		{
			if (N && (N->NodePosX != 0 || N->NodePosY != 0))
			{
				OutLocked.Add(N);
			}
		}
	}

	static void RunScriptStripLayout(
		UEdGraph* Graph,
		TArray<UEdGraphNode*>& ScriptNodes,
		const FUnrealBlueprintGraphFormatOptions& Options,
		FUnrealBlueprintGraphFormatResult& R)
	{
		TSet<UEdGraphNode*> LockedNodes;
		const TSet<UEdGraphNode*>* LockedPtr = nullptr;
		if (Options.bPreserveExistingPositions)
		{
			BuildPreserveLockedSet(ScriptNodes, LockedNodes);
			R.NodesSkippedPreserve = LockedNodes.Num();
			LockedPtr = &LockedNodes;
		}

		{
			UnrealBlueprintLayeredDagLayout::FLayeredLayoutStats St;
			UnrealBlueprintLayeredDagLayout::LayoutScriptNodes(Graph, ScriptNodes, Options, St);
			R.EntrySubgraphs = St.EntryPoints;
			R.DisconnectedNodes += St.DisconnectedNodes;
			R.DataOnlyNodesPlaced += St.DataOnlyNodes;
			R.NodesPositioned += St.LayoutNodes;
		}
		ResolveAxisAlignedOverlapsAmong(ScriptNodes, LockedPtr);
	}

	static void AccumulateMoveStats(
		const TMap<const UEdGraphNode*, FIntPoint>& Before,
		const TArray<UEdGraphNode*>& Nodes,
		FUnrealBlueprintGraphFormatResult& R)
	{
		for (UEdGraphNode* N : Nodes)
		{
			if (!N)
			{
				continue;
			}
			if (const FIntPoint* P = Before.Find(N))
			{
				if (P->X != N->NodePosX || P->Y != N->NodePosY)
				{
					++R.NodesMoved;
				}
			}
		}
	}

	/** After strip layout, move the cluster so its top-left origin matches the pre-layout cluster (avoids piling on Y=0 over unrelated nodes). */
	static void TranslateNodesPreservingSelectionOrigin(
		const TArray<UEdGraphNode*>& ScriptNodes,
		const TMap<const UEdGraphNode*, FIntPoint>& BeforePositions)
	{
		if (ScriptNodes.Num() == 0 || BeforePositions.Num() == 0)
		{
			return;
		}
		int32 OldMinX = MAX_int32;
		int32 OldMinY = MAX_int32;
		for (const TPair<const UEdGraphNode*, FIntPoint>& Pair : BeforePositions)
		{
			OldMinX = FMath::Min(OldMinX, Pair.Value.X);
			OldMinY = FMath::Min(OldMinY, Pair.Value.Y);
		}
		if (OldMinX == MAX_int32)
		{
			return;
		}
		int32 NewMinX = MAX_int32;
		int32 NewMinY = MAX_int32;
		for (UEdGraphNode* N : ScriptNodes)
		{
			if (!N)
			{
				continue;
			}
			NewMinX = FMath::Min(NewMinX, N->NodePosX);
			NewMinY = FMath::Min(NewMinY, N->NodePosY);
		}
		if (NewMinX == MAX_int32)
		{
			return;
		}
		const int32 DX = OldMinX - NewMinX;
		const int32 DY = OldMinY - NewMinY;
		if (DX == 0 && DY == 0)
		{
			return;
		}
		for (UEdGraphNode* N : ScriptNodes)
		{
			if (N)
			{
				N->NodePosX += DX;
				N->NodePosY += DY;
			}
		}
	}

	static bool UnionRectsOfNodes(const TArray<UEdGraphNode*>& Nodes, FNodeRect& OutUnion)
	{
		bool bAny = false;
		for (UEdGraphNode* N : Nodes)
		{
			if (!N)
			{
				continue;
			}
			const FNodeRect R = GetNodeRect(N);
			if (!bAny)
			{
				OutUnion = R;
				bAny = true;
			}
			else
			{
				OutUnion.MinX = FMath::Min(OutUnion.MinX, R.MinX);
				OutUnion.MinY = FMath::Min(OutUnion.MinY, R.MinY);
				OutUnion.MaxX = FMath::Max(OutUnion.MaxX, R.MaxX);
				OutUnion.MaxY = FMath::Max(OutUnion.MaxY, R.MaxY);
			}
		}
		return bAny;
	}

	static UEdGraphNode* FirstMaterializedAnchorForGap(
		const TArray<UEdGraphNode*>& MaterializedNodes)
	{
		for (UEdGraphNode* N : MaterializedNodes)
		{
			if (N && TakesPartInScriptLayout(N))
			{
				return N;
			}
		}
		for (UEdGraphNode* N : MaterializedNodes)
		{
			if (N)
			{
				return N;
			}
		}
		return nullptr;
	}

	/**
	 * Push non-patch script nodes to the right until none overlap the materialized cluster (AABB + pairwise gap).
	 * Iterates so chains of outside nodes clear each other.
	 */
	static void PushNonMaterializedScriptNodesClearingCluster(
		UEdGraph* Graph,
		const TSet<const UEdGraphNode*>& MSet,
		const TArray<UEdGraphNode*>& MaterializedNodes)
	{
		if (!Graph)
		{
			return;
		}
		UEdGraphNode* const AnchorMat = FirstMaterializedAnchorForGap(MaterializedNodes);
		constexpr int32 MaxPasses = 96;
		for (int32 Pass = 0; Pass < MaxPasses; ++Pass)
		{
			FNodeRect Cluster;
			if (!UnionRectsOfNodes(MaterializedNodes, Cluster))
			{
				return;
			}
			bool bMoved = false;
			for (UEdGraphNode* U : Graph->Nodes)
			{
				if (!U || MSet.Contains(U) || !TakesPartInScriptLayout(U))
				{
					continue;
				}
				const FNodeRect Ur = GetNodeRect(U);
				const int32 G = AnchorMat ? PairwiseSeparationGap(AnchorMat, U) : 64;
				if (!NodeRectsOverlapWithGap(Cluster, Ur, G))
				{
					continue;
				}
				const int32 Dx = (Cluster.MaxX + G) - Ur.MinX;
				if (Dx > 0)
				{
					U->NodePosX += Dx;
					bMoved = true;
				}
			}
			if (!bMoved)
			{
				break;
			}
		}
	}

	/**
	 * After laying out only patch/IR materialized nodes, the layered strip layout often has no "entry" event inside
	 * that subset (e.g. Enhanced Input node is outside the patch). Nodes then pack at X≈0, far from the real graph.
	 * Shift the whole cluster so it sits next to wired neighbors (exec predecessors first, else successors, else any link).
	 * Then push any non-materialized script nodes whose bounds overlap the cluster (full graph, not only an exec Y-slab).
	 *
	 * @param bRepositionCluster When false (explicit per-node x/y hints were applied), only run overlap push — do not add DX/DY,
	 *        so agent/introspect coordinates are preserved and we never pull hinted nodes to the origin or past a bogus succ anchor.
	 */
	static void TranslateMaterializedClusterTowardGraphNeighbors(
		UEdGraph* Graph,
		const TArray<UEdGraphNode*>& MaterializedNodes,
		const bool bRepositionCluster)
	{
		if (MaterializedNodes.Num() == 0)
		{
			return;
		}
		TSet<const UEdGraphNode*> MSet;
		for (UEdGraphNode* N : MaterializedNodes)
		{
			if (N)
			{
				MSet.Add(N);
			}
		}

		FNodeRect ClusterRect;
		if (!UnionRectsOfNodes(MaterializedNodes, ClusterRect))
		{
			return;
		}

		auto IsExec = [](const UEdGraphPin* P) -> bool
		{
			return P && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
		};

		int32 MaxPredRight = MIN_int32;
		int32 PredCyAccum = 0;
		int32 PredYCount = 0;
		int32 MinSuccLeft = MAX_int32;
		double AnyCx = 0.0;
		double AnyCy = 0.0;
		int32 AnyCnt = 0;

		for (UEdGraphNode* N : MaterializedNodes)
		{
			if (!N)
			{
				continue;
			}
			for (UEdGraphPin* Pin : N->Pins)
			{
				if (!Pin)
				{
					continue;
				}
				for (UEdGraphPin* L : Pin->LinkedTo)
				{
					if (!L)
					{
						continue;
					}
					UEdGraphNode* O = L->GetOwningNode();
					if (!O || MSet.Contains(O))
					{
						continue;
					}
					const FNodeRect Or = GetNodeRect(O);
					const int32 OCx = (Or.MinX + Or.MaxX) / 2;
					const int32 OCy = (Or.MinY + Or.MaxY) / 2;
					AnyCx += OCx;
					AnyCy += OCy;
					++AnyCnt;

					if (Pin->Direction == EGPD_Input && IsExec(Pin))
					{
						MaxPredRight = FMath::Max(MaxPredRight, Or.MaxX);
						PredCyAccum += OCy;
						++PredYCount;
					}
					if (Pin->Direction == EGPD_Output && IsExec(Pin) && !Pin->bHidden)
					{
						MinSuccLeft = FMath::Min(MinSuccLeft, Or.MinX);
					}
				}
			}
		}

		constexpr int32 GapPred = 400;
		constexpr int32 GapSucc = 360;

		int32 DX = 0;
		int32 DY = 0;
		if (bRepositionCluster && AnyCnt > 0)
		{
			const int32 ClusterMinX = ClusterRect.MinX;
			const int32 ClusterMaxX = ClusterRect.MaxX;
			const int32 ClusterMinY = ClusterRect.MinY;
			const int32 ClusterMidX = (ClusterMinX + ClusterMaxX) / 2;

			if (MaxPredRight != MIN_int32)
			{
				DX = (MaxPredRight + GapPred) - ClusterMinX;
			}
			else if (MinSuccLeft != MAX_int32 && MinSuccLeft > ClusterMaxX + 32)
			{
				DX = (MinSuccLeft - GapSucc) - ClusterMaxX;
			}
			else
			{
				const int32 TargetX = FMath::RoundToInt(AnyCx / AnyCnt) + GapPred;
				DX = TargetX - ClusterMidX;
			}

			if (PredYCount > 0)
			{
				DY = (PredCyAccum / PredYCount) - ClusterMinY;
			}
			else
			{
				DY = FMath::RoundToInt(AnyCy / AnyCnt) - ClusterMinY;
			}

			if (DX != 0 || DY != 0)
			{
				for (UEdGraphNode* N : MaterializedNodes)
				{
					if (N)
					{
						N->NodePosX += DX;
						N->NodePosY += DY;
					}
				}
			}
		}

		if (Graph)
		{
			PushNonMaterializedScriptNodesClearingCluster(Graph, MSet, MaterializedNodes);
		}
	}
}

FUnrealBlueprintGraphFormatResult FUnrealBlueprintGraphFormatService::LayoutAfterAiIrApply(
	UEdGraph* Graph,
	const TArray<UEdGraphNode*>& MaterializedNodes,
	const TArray<FUnrealBlueprintIrNodeLayoutHint>& Hints,
	const FUnrealBlueprintGraphFormatOptions& Options)
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
		UnrealBlueprintFormatServicePriv::TranslateMaterializedClusterTowardGraphNeighbors(Graph, MaterializedNodes, false);
		UnrealBlueprintFormatServicePriv::EnforceCommentContainmentAfterMove(Graph, MaterializedNodes, BeforePositions);
		UnrealBlueprintFormatServicePriv::RunPostLayoutPasses(Graph, Options, R);
		UnrealBlueprintFormatServicePriv::AccumulateMoveStats(BeforePositions, MaterializedNodes, R);
		return R;
	}

	TArray<UEdGraphNode*> Copy;
	Copy.Reserve(MaterializedNodes.Num());
	for (UEdGraphNode* N : MaterializedNodes)
	{
		if (UnrealBlueprintFormatServicePriv::TakesPartInScriptLayout(N))
		{
			Copy.Add(N);
		}
	}
	UnrealBlueprintFormatServicePriv::RunScriptStripLayout(Graph, Copy, Options, R);
	UnrealBlueprintFormatServicePriv::TranslateMaterializedClusterTowardGraphNeighbors(Graph, MaterializedNodes, true);
	UnrealBlueprintFormatServicePriv::EnforceCommentContainmentAfterMove(Graph, MaterializedNodes, BeforePositions);
	UnrealBlueprintFormatServicePriv::RunPostLayoutPasses(Graph, Options, R);
	return R;
}

FUnrealBlueprintGraphFormatResult FUnrealBlueprintGraphFormatService::LayoutEntireGraph(
	UEdGraph* Graph,
	const FUnrealBlueprintGraphFormatOptions& Options)
{
	FUnrealBlueprintGraphFormatResult R;
	if (!Graph)
	{
		return R;
	}
	TArray<UEdGraphNode*> ScriptNodes;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (UnrealBlueprintFormatServicePriv::TakesPartInScriptLayout(N))
		{
			ScriptNodes.Add(N);
		}
	}
	if (ScriptNodes.Num() == 0)
	{
		UnrealBlueprintFormatServicePriv::RunPostLayoutPasses(Graph, Options, R);
		return R;
	}
	const TMap<const UEdGraphNode*, FIntPoint> BeforePositions =
		UnrealBlueprintFormatServicePriv::SnapshotNodePositions(ScriptNodes);
	UnrealBlueprintFormatServicePriv::RunScriptStripLayout(Graph, ScriptNodes, Options, R);
	UnrealBlueprintFormatServicePriv::EnforceCommentContainmentAfterMove(Graph, ScriptNodes, BeforePositions);
	UnrealBlueprintFormatServicePriv::RunPostLayoutPasses(Graph, Options, R);
	UnrealBlueprintFormatServicePriv::AccumulateMoveStats(BeforePositions, ScriptNodes, R);
	return R;
}

FUnrealBlueprintGraphFormatResult FUnrealBlueprintGraphFormatService::LayoutSelectedNodes(
	UEdGraph* Graph,
	const TArray<UEdGraphNode*>& SelectedNodes,
	const FUnrealBlueprintGraphFormatOptions& Options)
{
	FUnrealBlueprintGraphFormatResult R;
	if (!Graph || SelectedNodes.Num() == 0)
	{
		return R;
	}

	TArray<UEdGraphNode*> ScriptNodes;
	ScriptNodes.Reserve(SelectedNodes.Num());
	for (UEdGraphNode* Node : SelectedNodes)
	{
		if (UnrealBlueprintFormatServicePriv::TakesPartInScriptLayout(Node))
		{
			ScriptNodes.Add(Node);
		}
	}

	if (ScriptNodes.Num() == 0)
	{
		UnrealBlueprintFormatServicePriv::RunPostLayoutPasses(Graph, Options, R);
		return R;
	}

	const TMap<const UEdGraphNode*, FIntPoint> BeforePositions =
		UnrealBlueprintFormatServicePriv::SnapshotNodePositions(ScriptNodes);
	UnrealBlueprintFormatServicePriv::RunScriptStripLayout(Graph, ScriptNodes, Options, R);
	UnrealBlueprintFormatServicePriv::TranslateNodesPreservingSelectionOrigin(ScriptNodes, BeforePositions);
	UnrealBlueprintFormatServicePriv::EnforceCommentContainmentAfterMove(Graph, ScriptNodes, BeforePositions);
	UnrealBlueprintFormatServicePriv::RunPostLayoutPasses(Graph, Options, R);
	UnrealBlueprintFormatServicePriv::AccumulateMoveStats(BeforePositions, ScriptNodes, R);
	return R;
}
