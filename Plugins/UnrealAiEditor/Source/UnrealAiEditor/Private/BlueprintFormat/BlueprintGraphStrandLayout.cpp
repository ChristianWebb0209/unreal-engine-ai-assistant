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

	static int32 ExecLaneHeightEstimate(const UEdGraphNode* N)
	{
		if (!N)
		{
			return 120;
		}
		return FMath::Max(120, N->NodeHeight > 0 ? N->NodeHeight + 80 : 200);
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
				const TArray<UEdGraphPin*>& Linked = Pin->LinkedTo;
				for (UEdGraphPin* LP : Linked)
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
		SortedSeeds.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return A.NodeGuid < B.NodeGuid;
		});
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
			Comp.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				return A.NodeGuid < B.NodeGuid;
			});
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
		Queue.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return A.NodeGuid < B.NodeGuid;
		});
		int32 Q = 0;
		while (Q < Queue.Num())
		{
			UEdGraphNode* Cur = Queue[Q++];
			OutOrder.Add(Cur);
			TArray<UEdGraphNode*> Children;
			Forward.MultiFind(Cur, Children);
			Children.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				return A.NodeGuid < B.NodeGuid;
			});
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
			Remaining.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				return A.NodeGuid < B.NodeGuid;
			});
			OutOrder.Append(Remaining);
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
	constexpr int32 DX = 400;
	const TArray<TArray<UEdGraphNode*>> Components = PartitionExecComponents(Nodes);
	int32 LaneY = 0;
	for (const TArray<UEdGraphNode*>& Comp : Components)
	{
		if (Comp.Num() == 0)
		{
			continue;
		}
		TArray<UEdGraphNode*> Order;
		TopologicalExecOrder(Comp, Order);
		int32 X = 0;
		int32 MaxLaneH = 0;
		for (UEdGraphNode* N : Order)
		{
			if (!N)
			{
				continue;
			}
			N->NodePosX = X;
			N->NodePosY = LaneY;
			X += DX;
			MaxLaneH = FMath::Max(MaxLaneH, ExecLaneHeightEstimate(N));
			++OutNodesPositioned;
		}
		LaneY += MaxLaneH;
	}
}
