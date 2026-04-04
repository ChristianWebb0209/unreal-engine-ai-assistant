// SPDX-License-Identifier: MIT

#include "BlueprintFormat/BlueprintGraphAutoComments.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"

#include "Containers/Queue.h"

namespace UnrealBlueprintAutoCommentsPriv
{
	static bool IsExecPin(const UEdGraphPin* P)
	{
		return P && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	static TArray<UEdGraphNode*> ExecSuccessors(UEdGraphNode* Node)
	{
		TArray<UEdGraphNode*> Succ;
		if (!Node)
		{
			return Succ;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || !IsExecPin(Pin) || Pin->bHidden)
			{
				continue;
			}
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

	static FString EntryTitle(UEdGraphNode* Entry)
	{
		if (!Entry)
		{
			return TEXT("Graph");
		}
		if (Cast<UK2Node_FunctionEntry>(Entry))
		{
			return TEXT("Function");
		}
		return Entry->GetNodeTitle(ENodeTitleType::ListView).ToString();
	}

	static void NodeBounds(UEdGraphNode* N, int32& MinX, int32& MinY, int32& MaxX, int32& MaxY)
	{
		const int32 W = FMath::Max(64, N && N->NodeWidth > 0 ? N->NodeWidth : 240);
		const int32 H = FMath::Max(32, N && N->NodeHeight > 0 ? N->NodeHeight : 120);
		MinX = N->NodePosX;
		MinY = N->NodePosY;
		MaxX = N->NodePosX + W;
		MaxY = N->NodePosY + H;
	}
}

int32 UnrealBlueprintAutoComments::MaybeAddRegionCommentsForLargeGraphs(
	UEdGraph* Graph,
	const FUnrealBlueprintGraphFormatOptions& Options)
{
	using namespace UnrealBlueprintAutoCommentsPriv;
	if (!Graph || Options.CommentsMode == EUnrealAiBlueprintCommentsMode::Off)
	{
		return 0;
	}

	const int32 MinNodes = Options.CommentsMode == EUnrealAiBlueprintCommentsMode::Verbose ? 6 : 12;

	TSet<UEdGraphNode*> ProcessedEntries;
	int32 Added = 0;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || Cast<UEdGraphNode_Comment>(Node))
		{
			continue;
		}
		if (!Node->IsA(UK2Node_Event::StaticClass()) && !Node->IsA(UK2Node_CustomEvent::StaticClass())
			&& !Cast<UK2Node_FunctionEntry>(Node))
		{
			continue;
		}
		if (ProcessedEntries.Contains(Node))
		{
			continue;
		}

		TSet<UEdGraphNode*> Comp;
		TQueue<UEdGraphNode*> Q;
		Q.Enqueue(Node);
		Comp.Add(Node);
		UEdGraphNode* Cur = nullptr;
		while (Q.Dequeue(Cur))
		{
			if (!Cur || Cast<UEdGraphNode_Comment>(Cur))
			{
				continue;
			}
			for (UEdGraphNode* S : ExecSuccessors(Cur))
			{
				if (S && !Cast<UEdGraphNode_Comment>(S) && !Comp.Contains(S))
				{
					Comp.Add(S);
					Q.Enqueue(S);
				}
			}
		}

		if (Comp.Num() < MinNodes)
		{
			continue;
		}

		for (UEdGraphNode* E : Comp)
		{
			if (E)
			{
				ProcessedEntries.Add(E);
			}
		}

		int32 MinX = MAX_int32, MinY = MAX_int32, MaxX = INT32_MIN, MaxY = INT32_MIN;
		for (UEdGraphNode* M : Comp)
		{
			if (!M)
			{
				continue;
			}
			int32 L, T, R, B;
			NodeBounds(M, L, T, R, B);
			MinX = FMath::Min(MinX, L);
			MinY = FMath::Min(MinY, T);
			MaxX = FMath::Max(MaxX, R);
			MaxY = FMath::Max(MaxY, B);
		}
		if (MinX == MAX_int32)
		{
			continue;
		}

		constexpr int32 Pad = 48;
		UEdGraphNode_Comment* C = NewObject<UEdGraphNode_Comment>(Graph);
		Graph->AddNode(C, true, false);
		C->NodePosX = MinX - Pad;
		C->NodePosY = MinY - Pad;
		C->CreateNewGuid();
		C->NodeComment = FString::Printf(TEXT("%s (%d nodes)"), *EntryTitle(Node), Comp.Num());
		C->NodeWidth = FMath::Max(128, MaxX - MinX + 2 * Pad);
		C->NodeHeight = FMath::Max(128, MaxY - MinY + 2 * Pad);
		C->PostPlacedNewNode();
		++Added;
	}

	return Added;
}
