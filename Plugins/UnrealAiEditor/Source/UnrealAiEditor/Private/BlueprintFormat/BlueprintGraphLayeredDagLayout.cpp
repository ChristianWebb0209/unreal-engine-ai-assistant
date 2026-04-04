// SPDX-License-Identifier: MIT

#include "BlueprintFormat/BlueprintGraphLayeredDagLayout.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"

#include "Containers/Queue.h"

namespace UnrealBlueprintLayeredDagLayoutPriv
{
	static bool IsExecPin(const UEdGraphPin* P)
	{
		return P && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	static TArray<UEdGraphNode*> ExecSuccessorsOrdered(UEdGraphNode* Node)
	{
		TArray<UEdGraphNode*> Succ;
		if (!Node)
		{
			return Succ;
		}
		TArray<UEdGraphPin*> OutExec;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && IsExecPin(Pin) && !Pin->bHidden)
			{
				OutExec.Add(Pin);
			}
		}
		OutExec.Sort([](const UEdGraphPin& A, const UEdGraphPin& B) { return A.PinName.LexicalLess(B.PinName); });
		for (UEdGraphPin* Pin : OutExec)
		{
			for (UEdGraphPin* L : Pin->LinkedTo)
			{
				if (UEdGraphNode* O = L ? L->GetOwningNode() : nullptr)
				{
					Succ.AddUnique(O);
				}
			}
		}
		return Succ;
	}

	static TArray<UEdGraphNode*> DataConsumers(UEdGraphNode* Node)
	{
		TArray<UEdGraphNode*> Consumers;
		if (!Node)
		{
			return Consumers;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || Pin->bHidden || IsExecPin(Pin))
			{
				continue;
			}
			for (UEdGraphPin* L : Pin->LinkedTo)
			{
				if (UEdGraphNode* O = L ? L->GetOwningNode() : nullptr)
				{
					Consumers.AddUnique(O);
				}
			}
		}
		return Consumers;
	}

	static TArray<UEdGraphNode*> FindEntries(UEdGraph* Graph, const TSet<UEdGraphNode*>& ScriptSet)
	{
		TArray<UEdGraphNode*> Entries;
		if (!Graph)
		{
			return Entries;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || !ScriptSet.Contains(Node))
			{
				continue;
			}
			if (Cast<UK2Node_Event>(Node) || Cast<UK2Node_CustomEvent>(Node) || Cast<UK2Node_FunctionEntry>(Node))
			{
				Entries.Add(Node);
			}
		}
		return Entries;
	}

	static TArray<TPair<UEdGraphNode*, UEdGraphNode*>> CollectExecEdges(const TArray<UEdGraphNode*>& ScriptNodes)
	{
		TArray<TPair<UEdGraphNode*, UEdGraphNode*>> Edges;
		TSet<UEdGraphNode*> ScriptSet(ScriptNodes);
		for (UEdGraphNode* U : ScriptNodes)
		{
			if (!U)
			{
				continue;
			}
			for (UEdGraphNode* V : ExecSuccessorsOrdered(U))
			{
				if (V && ScriptSet.Contains(V))
				{
					Edges.Add(TPair<UEdGraphNode*, UEdGraphNode*>(U, V));
				}
			}
		}
		return Edges;
	}

	static void MarkExecReachable(UEdGraphNode* Seed, const TSet<UEdGraphNode*>& ScriptSet, TSet<UEdGraphNode*>& OutReachable)
	{
		TQueue<UEdGraphNode*> Q;
		Q.Enqueue(Seed);
		OutReachable.Add(Seed);
		UEdGraphNode* Cur = nullptr;
		while (Q.Dequeue(Cur))
		{
			if (!Cur)
			{
				continue;
			}
			for (UEdGraphNode* S : ExecSuccessorsOrdered(Cur))
			{
				if (S && ScriptSet.Contains(S) && !OutReachable.Contains(S))
				{
					OutReachable.Add(S);
					Q.Enqueue(S);
				}
			}
		}
	}
}

void UnrealBlueprintLayeredDagLayout::LayoutScriptNodes(
	UEdGraph* Graph,
	const TArray<UEdGraphNode*>& ScriptNodes,
	const FUnrealBlueprintGraphFormatOptions& Options,
	FLayeredLayoutStats& OutStats)
{
	using namespace UnrealBlueprintLayeredDagLayoutPriv;
	OutStats = FLayeredLayoutStats();
	if (!Graph || ScriptNodes.Num() == 0)
	{
		return;
	}

	TSet<UEdGraphNode*> ScriptSet(ScriptNodes);
	TSet<UEdGraphNode*> SkippedSet;
	if (Options.bPreserveExistingPositions)
	{
		for (UEdGraphNode* N : ScriptNodes)
		{
			if (N && (N->NodePosX != 0 || N->NodePosY != 0))
			{
				SkippedSet.Add(N);
				++OutStats.SkippedPreserve;
			}
		}
	}

	const TArray<UEdGraphNode*> Entries = FindEntries(Graph, ScriptSet);
	OutStats.EntryPoints = Entries.Num();
	TSet<UEdGraphNode*> ExecReachable;
	for (UEdGraphNode* E : Entries)
	{
		if (E)
		{
			MarkExecReachable(E, ScriptSet, ExecReachable);
		}
	}

	const TArray<TPair<UEdGraphNode*, UEdGraphNode*>> Edges = CollectExecEdges(ScriptNodes);
	TMap<UEdGraphNode*, int32> Layer;
	for (UEdGraphNode* N : ScriptNodes)
	{
		if (N)
		{
			Layer.Add(N, ExecReachable.Contains(N) ? -1 : -2);
		}
	}

	const int32 SpacingX = FMath::Max(64, Options.SpacingX);
	for (UEdGraphNode* E : Entries)
	{
		if (!E)
		{
			continue;
		}
		if (!SkippedSet.Contains(E))
		{
			Layer[E] = 0;
		}
		else
		{
			Layer[E] = FMath::Max(0, E->NodePosX / SpacingX);
		}
	}

	const int32 MaxIter = FMath::Min(256, ScriptNodes.Num() + 8);
	for (int32 It = 0; It < MaxIter; ++It)
	{
		bool bChanged = false;
		for (const TPair<UEdGraphNode*, UEdGraphNode*>& Edge : Edges)
		{
			UEdGraphNode* U = Edge.Key;
			UEdGraphNode* V = Edge.Value;
			if (!U || !V || !ExecReachable.Contains(V))
			{
				continue;
			}
			const int32 Lu = Layer.FindRef(U);
			if (Lu < 0)
			{
				continue;
			}
			if (SkippedSet.Contains(V))
			{
				continue;
			}
			const int32 Candidate = Lu + 1;
			if (Layer[V] < Candidate)
			{
				Layer[V] = Candidate;
				bChanged = true;
			}
		}
		if (!bChanged)
		{
			break;
		}
	}

	for (UEdGraphNode* N : ScriptNodes)
	{
		if (!N || !ExecReachable.Contains(N) || SkippedSet.Contains(N))
		{
			continue;
		}
		if (Layer[N] < 0)
		{
			Layer[N] = 0;
			++OutStats.DisconnectedNodes;
		}
	}

	TMap<int32, TArray<UEdGraphNode*>> Buckets;
	int32 MaxLayer = 0;
	for (UEdGraphNode* N : ScriptNodes)
	{
		if (!N || !ExecReachable.Contains(N) || SkippedSet.Contains(N))
		{
			continue;
		}
		const int32 L = Layer[N];
		MaxLayer = FMath::Max(MaxLayer, L);
		Buckets.FindOrAdd(L).Add(N);
	}

	for (TPair<int32, TArray<UEdGraphNode*>>& Pair : Buckets)
	{
		Pair.Value.Sort([](UEdGraphNode& A, UEdGraphNode& B) { return A.NodeGuid < B.NodeGuid; });
	}

	auto IndexInLayer = [&Buckets](UEdGraphNode* N) -> int32
	{
		for (const TPair<int32, TArray<UEdGraphNode*>>& P : Buckets)
		{
			const int32 Idx = P.Value.IndexOfByKey(N);
			if (Idx != INDEX_NONE)
			{
				return Idx;
			}
		}
		return 0;
	};

	auto LayerOf = [&Buckets](UEdGraphNode* N) -> int32
	{
		for (const TPair<int32, TArray<UEdGraphNode*>>& P : Buckets)
		{
			if (P.Value.Contains(N))
			{
				return P.Key;
			}
		}
		return 0;
	};

	for (int32 Sweep = 0; Sweep < 2; ++Sweep)
	{
		for (int32 L = 1; L <= MaxLayer; ++L)
		{
			TArray<UEdGraphNode*>& Row = Buckets.FindOrAdd(L);
			TArray<float> Medians;
			Medians.Reserve(Row.Num());
			for (UEdGraphNode* N : Row)
			{
				float Sum = 0.f;
				int32 Cnt = 0;
				for (const TPair<UEdGraphNode*, UEdGraphNode*>& Edge : Edges)
				{
					if (Edge.Value == N && ExecReachable.Contains(Edge.Key))
					{
						Sum += static_cast<float>(IndexInLayer(Edge.Key));
						++Cnt;
					}
				}
				Medians.Add(Cnt > 0 ? Sum / Cnt : 0.f);
			}
			Row.Sort([&Medians, &Row](UEdGraphNode& A, UEdGraphNode& B)
			{
				const int32 IA = Row.IndexOfByKey(&A);
				const int32 IB = Row.IndexOfByKey(&B);
				const float MA = IA != INDEX_NONE ? Medians[IA] : 0.f;
				const float MB = IB != INDEX_NONE ? Medians[IB] : 0.f;
				if (MA != MB)
				{
					return MA < MB;
				}
				return A.NodeGuid < B.NodeGuid;
			});
		}
		for (int32 L = MaxLayer - 1; L >= 0; --L)
		{
			TArray<UEdGraphNode*>& Row = Buckets.FindOrAdd(L);
			TArray<float> Medians;
			Medians.Reserve(Row.Num());
			for (UEdGraphNode* N : Row)
			{
				float Sum = 0.f;
				int32 Cnt = 0;
				for (UEdGraphNode* Succ : ExecSuccessorsOrdered(N))
				{
					if (!Succ || LayerOf(Succ) != L + 1)
					{
						continue;
					}
					Sum += static_cast<float>(IndexInLayer(Succ));
					++Cnt;
				}
				Medians.Add(Cnt > 0 ? Sum / Cnt : 0.f);
			}
			Row.Sort([&Medians, &Row](UEdGraphNode& A, UEdGraphNode& B)
			{
				const int32 IA = Row.IndexOfByKey(&A);
				const int32 IB = Row.IndexOfByKey(&B);
				const float MA = IA != INDEX_NONE ? Medians[IA] : 0.f;
				const float MB = IB != INDEX_NONE ? Medians[IB] : 0.f;
				if (MA != MB)
				{
					return MA < MB;
				}
				return A.NodeGuid < B.NodeGuid;
			});
		}
	}

	const int32 SpacingY = FMath::Max(32, Options.SpacingY);
	const int32 BranchStep = FMath::Max(1, Options.BranchVerticalGap / FMath::Max(8, SpacingY / 4));

	TMap<UEdGraphNode*, int32> YIndex;
	int32 GlobalBand = 0;
	for (int32 L = 0; L <= MaxLayer; ++L)
	{
		TArray<UEdGraphNode*> Row = Buckets.FindRef(L);
		for (int32 I = 0; I < Row.Num(); ++I)
		{
			UEdGraphNode* N = Row[I];
			if (!N || SkippedSet.Contains(N))
			{
				continue;
			}
			const int32 Y = GlobalBand + I * BranchStep;
			YIndex.Add(N, Y);
		}
		GlobalBand += FMath::Max(1, Row.Num()) * BranchStep + 2;
	}

	TMap<UEdGraphNode*, int32> DepthMap;
	for (UEdGraphNode* N : ScriptNodes)
	{
		if (N && ExecReachable.Contains(N) && !SkippedSet.Contains(N))
		{
			DepthMap.Add(N, Layer[N]);
		}
	}

	TSet<TPair<int32, int32>> OccupiedSlots;
	for (const TPair<UEdGraphNode*, int32>& P : DepthMap)
	{
		if (P.Key && YIndex.Contains(P.Key))
		{
			OccupiedSlots.Add(TPair<int32, int32>(P.Value, YIndex[P.Key]));
		}
	}

	int32 DataGlobalY = GlobalBand;
	for (UEdGraphNode* Node : ScriptNodes)
	{
		if (!Node || ExecReachable.Contains(Node) || SkippedSet.Contains(Node))
		{
			continue;
		}

		TArray<UEdGraphNode*> Consumers = DataConsumers(Node);
		UEdGraphNode* BestConsumer = nullptr;
		for (UEdGraphNode* C : Consumers)
		{
			if (!C || !DepthMap.Contains(C))
			{
				continue;
			}
			if (!BestConsumer || DepthMap[C] < DepthMap[BestConsumer])
			{
				BestConsumer = C;
			}
		}

		if (BestConsumer)
		{
			const int32 ConsumerDepth = DepthMap[BestConsumer];
			const int32 ConsumerY = YIndex.FindRef(BestConsumer);
			const int32 DataDepth = FMath::Max(0, ConsumerDepth - 1);
			int32 CandidateY = ConsumerY;
			while (OccupiedSlots.Contains(TPair<int32, int32>(DataDepth, CandidateY)))
			{
				++CandidateY;
			}
			OccupiedSlots.Add(TPair<int32, int32>(DataDepth, CandidateY));
			DepthMap.Add(Node, DataDepth);
			YIndex.Add(Node, CandidateY);
			Layer.Add(Node, DataDepth);
			++OutStats.DataOnlyNodes;
			DataGlobalY = FMath::Max(DataGlobalY, CandidateY + 1);
		}
	}

	for (UEdGraphNode* Node : ScriptNodes)
	{
		if (!Node || SkippedSet.Contains(Node))
		{
			continue;
		}
		if (!YIndex.Contains(Node))
		{
			const int32 D = 0;
			int32 CandidateY = DataGlobalY++;
			while (OccupiedSlots.Contains(TPair<int32, int32>(D, CandidateY)))
			{
				++CandidateY;
			}
			OccupiedSlots.Add(TPair<int32, int32>(D, CandidateY));
			DepthMap.Add(Node, D);
			YIndex.Add(Node, CandidateY);
			++OutStats.DisconnectedNodes;
		}
	}

	for (UEdGraphNode* N : ScriptNodes)
	{
		if (!N || SkippedSet.Contains(N))
		{
			continue;
		}
		const int32 L = DepthMap.FindRef(N);
		const int32 Y = YIndex.FindRef(N);
		N->NodePosX = L * SpacingX;
		N->NodePosY = Y * SpacingY;
		++OutStats.LayoutNodes;
	}
}
