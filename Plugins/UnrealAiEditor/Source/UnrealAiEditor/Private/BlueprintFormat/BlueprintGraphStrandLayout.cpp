// SPDX-License-Identifier: MIT

#include "BlueprintFormat/BlueprintGraphStrandLayout.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"

namespace UnrealBlueprintStrandLayoutPriv
{
	static bool IsExecPin(const UEdGraphPin* P)
	{
		return P && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	static int32 NodeWidth(const UEdGraphNode* N)
	{
		return FMath::Max(64, N && N->NodeWidth > 0 ? N->NodeWidth : 240);
	}

	static int32 NodeHeight(const UEdGraphNode* N)
	{
		return FMath::Max(32, N && N->NodeHeight > 0 ? N->NodeHeight : 120);
	}

	/** Horizontal space after a column: scales with the widest node in that column (avoids a fixed pixel gap). */
	static int32 ColumnGapAfterBlock(const int32 MaxWidthOfColumn)
	{
		return FMath::Max(36, MaxWidthOfColumn / 5);
	}

	/** Vertical gap between disconnected exec flows stacks from the bottom of the prior flow using each flow's tallest node. */
	static int32 VerticalStrandGapBetweenFlows(const int32 TallestNodeHPrev, const int32 TallestNodeHCur)
	{
		return FMath::Max(72, (TallestNodeHPrev + TallestNodeHCur + 2) / 3 + 32);
	}

	static int32 MaxNodeHeightInList(const TArray<UEdGraphNode*>& Comp)
	{
		int32 M = 32;
		for (UEdGraphNode* N : Comp)
		{
			if (N)
			{
				M = FMath::Max(M, NodeHeight(N));
			}
		}
		return M;
	}

	static void ComputeComponentAABB(
		const TArray<UEdGraphNode*>& Comp,
		int32& OutMinX,
		int32& OutMinY,
		int32& OutMaxX,
		int32& OutMaxY)
	{
		OutMinX = MAX_int32;
		OutMinY = MAX_int32;
		OutMaxX = INT32_MIN;
		OutMaxY = INT32_MIN;
		for (UEdGraphNode* N : Comp)
		{
			if (!N)
			{
				continue;
			}
			const int32 L = N->NodePosX;
			const int32 T = N->NodePosY;
			const int32 R = L + NodeWidth(N);
			const int32 B = T + NodeHeight(N);
			OutMinX = FMath::Min(OutMinX, L);
			OutMinY = FMath::Min(OutMinY, T);
			OutMaxX = FMath::Max(OutMaxX, R);
			OutMaxY = FMath::Max(OutMaxY, B);
		}
	}

	static void CollectComponentNodes(
		UEdGraphNode* Seed,
		const TMap<UEdGraphNode*, int32>& IndexOf,
		TArray<UEdGraphNode*>& OutComponent)
	{
		TSet<UEdGraphNode*> Visited;
		TArray<UEdGraphNode*> Stack;
		Stack.Add(Seed);
		while (Stack.Num() > 0)
		{
			UEdGraphNode* Cur = Stack.Pop(EAllowShrinking::No);
			if (!Cur || Visited.Contains(Cur))
			{
				continue;
			}
			if (!IndexOf.Contains(Cur))
			{
				continue;
			}
			Visited.Add(Cur);
			OutComponent.Add(Cur);
			for (UEdGraphPin* Pin : Cur->Pins)
			{
				if (!Pin || !IsExecPin(Pin))
				{
					continue;
				}
				for (UEdGraphPin* LP : Pin->LinkedTo)
				{
					if (UEdGraphNode* Other = LP ? LP->GetOwningNode() : nullptr)
					{
						if (IndexOf.Contains(Other) && !Visited.Contains(Other))
						{
							Stack.Add(Other);
						}
					}
				}
			}
		}
	}

	static TArray<TArray<UEdGraphNode*>> PartitionExecComponents(const TArray<UEdGraphNode*>& Nodes)
	{
		TMap<UEdGraphNode*, int32> IndexOf;
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			if (Nodes[i])
			{
				IndexOf.Add(Nodes[i], i);
			}
		}
		TSet<UEdGraphNode*> Assigned;
		TArray<TArray<UEdGraphNode*>> Components;
		TArray<UEdGraphNode*> SortedSeeds = Nodes;
		SortedSeeds.Sort([](const UEdGraphNode& A, const UEdGraphNode& B) { return A.NodeGuid < B.NodeGuid; });
		for (UEdGraphNode* Seed : SortedSeeds)
		{
			if (!Seed || Assigned.Contains(Seed))
			{
				continue;
			}
			TArray<UEdGraphNode*> Comp;
			CollectComponentNodes(Seed, IndexOf, Comp);
			for (UEdGraphNode* M : Comp)
			{
				Assigned.Add(M);
			}
			Comp.Sort([](const UEdGraphNode& A, const UEdGraphNode& B) { return A.NodeGuid < B.NodeGuid; });
			Components.Add(MoveTemp(Comp));
		}
		Components.Sort([](const TArray<UEdGraphNode*>& A, const TArray<UEdGraphNode*>& B)
		{
			const FGuid& GA = A.Num() > 0 && A[0] ? A[0]->NodeGuid : FGuid();
			const FGuid& GB = B.Num() > 0 && B[0] ? B[0]->NodeGuid : FGuid();
			return GA < GB;
		});
		return Components;
	}

	static void TopologicalExecOrder(const TArray<UEdGraphNode*>& Component, TArray<UEdGraphNode*>& OutOrder)
	{
		TSet<UEdGraphNode*> InSet;
		for (UEdGraphNode* N : Component)
		{
			if (N)
			{
				InSet.Add(N);
			}
		}
		TMap<UEdGraphNode*, int32> InDegree;
		TMultiMap<UEdGraphNode*, UEdGraphNode*> Forward;
		for (UEdGraphNode* N : Component)
		{
			if (!N)
			{
				continue;
			}
			InDegree.FindOrAdd(N, 0);
		}
		for (UEdGraphNode* N : Component)
		{
			if (!N)
			{
				continue;
			}
			for (UEdGraphPin* Pin : N->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output || !IsExecPin(Pin))
				{
					continue;
				}
				for (UEdGraphPin* LP : Pin->LinkedTo)
				{
					if (!LP || LP->Direction != EGPD_Input || !IsExecPin(LP))
					{
						continue;
					}
					UEdGraphNode* To = LP->GetOwningNode();
					if (To && InSet.Contains(To))
					{
						Forward.Add(N, To);
						InDegree.FindOrAdd(To)++;
					}
				}
			}
		}
		TArray<UEdGraphNode*> Queue;
		for (const TPair<UEdGraphNode*, int32>& Pair : InDegree)
		{
			if (Pair.Key && Pair.Value == 0)
			{
				Queue.Add(Pair.Key);
			}
		}
		Queue.Sort([](const UEdGraphNode& A, const UEdGraphNode& B) { return A.NodeGuid < B.NodeGuid; });
		int32 Q = 0;
		while (Q < Queue.Num())
		{
			UEdGraphNode* Cur = Queue[Q++];
			OutOrder.Add(Cur);
			TArray<UEdGraphNode*> Children;
			Forward.MultiFind(Cur, Children);
			Children.Sort([](const UEdGraphNode& A, const UEdGraphNode& B) { return A.NodeGuid < B.NodeGuid; });
			for (UEdGraphNode* Ch : Children)
			{
				int32& Deg = InDegree.FindOrAdd(Ch);
				Deg--;
				if (Deg == 0)
				{
					Queue.Add(Ch);
				}
			}
		}
		if (OutOrder.Num() != Component.Num())
		{
			TArray<UEdGraphNode*> Remaining;
			for (UEdGraphNode* N : Component)
			{
				if (N && !OutOrder.Contains(N))
				{
					Remaining.Add(N);
				}
			}
			Remaining.Sort([](const UEdGraphNode& A, const UEdGraphNode& B) { return A.NodeGuid < B.NodeGuid; });
			OutOrder.Append(Remaining);
		}
	}

	static void BuildExecPredecessors(
		const TArray<UEdGraphNode*>& Component,
		TMap<UEdGraphNode*, TArray<UEdGraphNode*>>& OutPreds)
	{
		OutPreds.Empty();
		TSet<UEdGraphNode*> InSet(Component);
		for (UEdGraphNode* From : Component)
		{
			if (!From)
			{
				continue;
			}
			for (UEdGraphPin* Pin : From->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output || !IsExecPin(Pin))
				{
					continue;
				}
				for (UEdGraphPin* LP : Pin->LinkedTo)
				{
					if (!LP || LP->Direction != EGPD_Input || !IsExecPin(LP))
					{
						continue;
					}
					UEdGraphNode* To = LP->GetOwningNode();
					if (To && InSet.Contains(To))
					{
						OutPreds.FindOrAdd(To).AddUnique(From);
					}
				}
			}
		}
	}

	/** Deterministic exec children (pin declaration order); first duplicate edge to a child wins. */
	static void GetOrderedExecChildren(
		UEdGraphNode* Parent,
		const TSet<UEdGraphNode*>& InComponent,
		TArray<UEdGraphNode*>& OutChildren)
	{
		OutChildren.Reset();
		struct FKeyed
		{
			int32 PinIdx = 0;
			int32 LinkIdx = 0;
			FGuid ChildGuid;
			UEdGraphNode* Child = nullptr;
		};
		TArray<FKeyed> Row;
		for (int32 Pi = 0; Pi < Parent->Pins.Num(); ++Pi)
		{
			UEdGraphPin* const Pin = Parent->Pins[Pi];
			if (!Pin || Pin->Direction != EGPD_Output || !IsExecPin(Pin))
			{
				continue;
			}
			for (int32 Li = 0; Li < Pin->LinkedTo.Num(); ++Li)
			{
				UEdGraphPin* const LP = Pin->LinkedTo[Li];
				if (!LP || LP->Direction != EGPD_Input || !IsExecPin(LP))
				{
					continue;
				}
				UEdGraphNode* const Ch = LP->GetOwningNode();
				if (!Ch || !InComponent.Contains(Ch))
				{
					continue;
				}
				FKeyed K;
				K.PinIdx = Pi;
				K.LinkIdx = Li;
				K.ChildGuid = Ch->NodeGuid;
				K.Child = Ch;
				Row.Add(K);
			}
		}
		Row.Sort([](const FKeyed& A, const FKeyed& B)
		{
			if (A.PinIdx != B.PinIdx)
			{
				return A.PinIdx < B.PinIdx;
			}
			if (A.LinkIdx != B.LinkIdx)
			{
				return A.LinkIdx < B.LinkIdx;
			}
			return A.ChildGuid < B.ChildGuid;
		});
		TSet<UEdGraphNode*> Seen;
		for (const FKeyed& K : Row)
		{
			if (Seen.Contains(K.Child))
			{
				continue;
			}
			Seen.Add(K.Child);
			OutChildren.Add(K.Child);
		}
	}

	/** Branch 0 up (+), 1 down (-), 2 further up, 3 further down… (only used when parent has 2+ exec children). */
	static int32 BranchVerticalOffsetFromIndex(const int32 ChildIndex, const int32 BranchPitch)
	{
		const int32 Mag = (ChildIndex / 2) + 1;
		const int32 Sign = (ChildIndex % 2 == 0) ? 1 : -1;
		return Sign * Mag * BranchPitch;
	}

	static int32 BranchOffsetParentToChild(
		UEdGraphNode* Parent,
		UEdGraphNode* Child,
		const TSet<UEdGraphNode*>& InComponent,
		const int32 BranchPitch)
	{
		TArray<UEdGraphNode*> Ch;
		GetOrderedExecChildren(Parent, InComponent, Ch);
		if (Ch.Num() <= 1)
		{
			return 0;
		}
		const int32 Idx = Ch.IndexOfByKey(Child);
		if (Idx == INDEX_NONE)
		{
			return 0;
		}
		return BranchVerticalOffsetFromIndex(Idx, BranchPitch);
	}

	static void ComputeExecDepths(
		const TArray<UEdGraphNode*>& Topo,
		const TMap<UEdGraphNode*, TArray<UEdGraphNode*>>& Preds,
		TMap<UEdGraphNode*, int32>& OutDepth)
	{
		OutDepth.Empty();
		for (UEdGraphNode* N : Topo)
		{
			if (!N)
			{
				continue;
			}
			int32 D = 0;
			if (const TArray<UEdGraphNode*>* PList = Preds.Find(N))
			{
				for (UEdGraphNode* P : *PList)
				{
					if (!P)
					{
						continue;
					}
					D = FMath::Max(D, OutDepth.FindOrAdd(P) + 1);
				}
			}
			OutDepth.Add(N, D);
		}
	}

	static void ComputeRelativeYByLayer(
		const TArray<UEdGraphNode*>& Component,
		const TArray<UEdGraphNode*>& Topo,
		const TMap<UEdGraphNode*, TArray<UEdGraphNode*>>& Preds,
		const TSet<UEdGraphNode*>& InSet,
		const int32 BranchPitch,
		TMap<UEdGraphNode*, int32>& OutYRel)
	{
		TMap<UEdGraphNode*, int32> Depth;
		ComputeExecDepths(Topo, Preds, Depth);
		int32 MaxDepth = 0;
		for (const TPair<UEdGraphNode*, int32>& Pair : Depth)
		{
			MaxDepth = FMath::Max(MaxDepth, Pair.Value);
		}
		OutYRel.Empty();
		for (int32 Level = 0; Level <= MaxDepth; ++Level)
		{
			for (UEdGraphNode* N : Component)
			{
				if (!N || Depth.FindOrAdd(N) != Level)
				{
					continue;
				}
				const TArray<UEdGraphNode*>* const PList = Preds.Find(N);
				if (!PList || PList->Num() == 0)
				{
					OutYRel.Add(N, 0);
				}
				else
				{
					int64 Sum = 0;
					for (UEdGraphNode* P : *PList)
					{
						Sum += (int64)OutYRel.FindOrAdd(P)
							+ (int64)BranchOffsetParentToChild(P, N, InSet, BranchPitch);
					}
					OutYRel.Add(N, (int32)(Sum / (int64)PList->Num()));
				}
			}
		}
		int32 MinY = MAX_int32;
		for (const TPair<UEdGraphNode*, int32>& Pair : OutYRel)
		{
			MinY = FMath::Min(MinY, Pair.Value);
		}
		if (MinY != MAX_int32 && MinY != 0)
		{
			for (TPair<UEdGraphNode*, int32>& Pair : OutYRel)
			{
				Pair.Value -= MinY;
			}
		}
	}

	/** Intrinsic placement: origin at Y=0 for the top of this flow; caller shifts entire flow below prior flows. */
	static void LayoutExecFlowIntrinsic(const TArray<UEdGraphNode*>& Comp, const int32 BranchPitch, int32& OutPositioned)
	{
		OutPositioned = 0;
		if (Comp.Num() == 0)
		{
			return;
		}

		TArray<UEdGraphNode*> Topo;
		TopologicalExecOrder(Comp, Topo);

		TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Preds;
		BuildExecPredecessors(Comp, Preds);

		TSet<UEdGraphNode*> InSet(Comp);
		TMap<UEdGraphNode*, int32> YRel;
		ComputeRelativeYByLayer(Comp, Topo, Preds, InSet, BranchPitch, YRel);

		TMap<UEdGraphNode*, int32> Depth;
		ComputeExecDepths(Topo, Preds, Depth);
		int32 MaxDepth = 0;
		for (UEdGraphNode* N : Comp)
		{
			if (N)
			{
				MaxDepth = FMath::Max(MaxDepth, Depth.FindOrAdd(N));
			}
		}

		TArray<int32> MaxColW;
		MaxColW.Init(0, MaxDepth + 1);
		for (UEdGraphNode* N : Comp)
		{
			if (!N)
			{
				continue;
			}
			const int32 D = Depth.FindOrAdd(N);
			MaxColW[D] = FMath::Max(MaxColW[D], NodeWidth(N));
		}

		TArray<int32> ColStart;
		ColStart.Init(0, MaxDepth + 1);
		for (int32 D = 1; D <= MaxDepth; ++D)
		{
			ColStart[D] = ColStart[D - 1] + MaxColW[D - 1] + ColumnGapAfterBlock(MaxColW[D - 1]);
		}

		for (UEdGraphNode* N : Comp)
		{
			if (!N)
			{
				continue;
			}
			const int32 D = Depth.FindOrAdd(N);
			N->NodePosX = ColStart[D];
			N->NodePosY = YRel.FindOrAdd(N);
			++OutPositioned;
		}
	}
}

void UnrealBlueprintStrandLayout::LayoutNodesMultiStrand(
	UEdGraph* Graph,
	const TArray<UEdGraphNode*>& Nodes,
	int32& OutNodesPositioned)
{
	using namespace UnrealBlueprintStrandLayoutPriv;
	OutNodesPositioned = 0;
	if (!Graph || Nodes.Num() == 0)
	{
		return;
	}
	const int32 BranchPitch = FMath::Max(96, 140);
	const TArray<TArray<UEdGraphNode*>> Components = PartitionExecComponents(Nodes);

	int32 StackBottom = 0;
	int32 PrevTallestH = 0;
	bool bHaveStackedAbove = false;

	for (const TArray<UEdGraphNode*>& Comp : Components)
	{
		if (Comp.Num() == 0)
		{
			continue;
		}

		int32 N = 0;
		LayoutExecFlowIntrinsic(Comp, BranchPitch, N);
		OutNodesPositioned += N;

		int32 BBMinX = 0;
		int32 BBMinY = 0;
		int32 BBMaxX = 0;
		int32 BBMaxY = 0;
		ComputeComponentAABB(Comp, BBMinX, BBMinY, BBMaxX, BBMaxY);
		if (BBMinX == MAX_int32)
		{
			continue;
		}

		const int32 CurTallest = MaxNodeHeightInList(Comp);
		const int32 VGap = bHaveStackedAbove ? VerticalStrandGapBetweenFlows(PrevTallestH, CurTallest) : 0;
		const int32 TargetTopY = bHaveStackedAbove ? (StackBottom + VGap) : BBMinY;
		const int32 DeltaY = TargetTopY - BBMinY;

		if (DeltaY != 0)
		{
			for (UEdGraphNode* Node : Comp)
			{
				if (Node)
				{
					Node->NodePosY += DeltaY;
				}
			}
		}

		ComputeComponentAABB(Comp, BBMinX, BBMinY, BBMaxX, BBMaxY);
		if (BBMinX != MAX_int32)
		{
			StackBottom = BBMaxY;
			PrevTallestH = CurTallest;
			bHaveStackedAbove = true;
		}
	}
}
