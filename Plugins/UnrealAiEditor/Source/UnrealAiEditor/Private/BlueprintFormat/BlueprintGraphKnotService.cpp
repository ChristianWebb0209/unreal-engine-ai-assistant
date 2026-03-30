// SPDX-License-Identifier: MIT

#include "BlueprintFormat/BlueprintGraphKnotService.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Knot.h"

namespace UnrealBlueprintKnotServicePriv
{
	static bool IsExecPin(const UEdGraphPin* P)
	{
		return P && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	static int32 DistanceThreshold(EUnrealBlueprintWireKnotAggression A)
	{
		switch (A)
		{
		case EUnrealBlueprintWireKnotAggression::Light:
			return 720;
		case EUnrealBlueprintWireKnotAggression::Aggressive:
			return 420;
		default:
			return TNumericLimits<int32>::Max();
		}
	}

	static int32 NodeCenterX(const UEdGraphNode* N)
	{
		if (!N)
		{
			return 0;
		}
		const int32 W = FMath::Max(64, N->NodeWidth > 0 ? N->NodeWidth : 240);
		return N->NodePosX + W / 2;
	}

	static int32 NodeCenterY(const UEdGraphNode* N)
	{
		if (!N)
		{
			return 0;
		}
		const int32 H = FMath::Max(32, N->NodeHeight > 0 ? N->NodeHeight : 120);
		return N->NodePosY + H / 2;
	}
}

int32 UnrealBlueprintKnotService::InsertDataWireKnots(UEdGraph* Graph, EUnrealBlueprintWireKnotAggression Aggression)
{
	using namespace UnrealBlueprintKnotServicePriv;
	if (!Graph || Aggression == EUnrealBlueprintWireKnotAggression::Off)
	{
		return 0;
	}
	const int32 Threshold = DistanceThreshold(Aggression);
	if (Threshold >= TNumericLimits<int32>::Max() / 2)
	{
		return 0;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	int32 KnotsAdded = 0;

	TArray<TPair<UEdGraphPin*, UEdGraphPin*>> Links;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || IsExecPin(Pin))
			{
				continue;
			}
			for (UEdGraphPin* Other : Pin->LinkedTo)
			{
				if (!Other || Other->Direction != EGPD_Input || IsExecPin(Other))
				{
					continue;
				}
				// One entry per undirected pair: only record from lower-guid node to avoid duplicates.
				UEdGraphNode* A = Pin->GetOwningNode();
				UEdGraphNode* B = Other->GetOwningNode();
				if (!A || !B || A == B)
				{
					continue;
				}
				if (Cast<UK2Node_Knot>(A) || Cast<UK2Node_Knot>(B))
				{
					continue;
				}
				if (LexToString(A->NodeGuid) > LexToString(B->NodeGuid))
				{
					continue;
				}
				Links.Add(TPair<UEdGraphPin*, UEdGraphPin*>(Pin, Other));
			}
		}
	}

	for (const TPair<UEdGraphPin*, UEdGraphPin*>& Link : Links)
	{
		UEdGraphPin* FromOut = Link.Key;
		UEdGraphPin* ToIn = Link.Value;
		if (!FromOut || !ToIn || !FromOut->GetOwningNode() || !ToIn->GetOwningNode())
		{
			continue;
		}
		const int32 X0 = NodeCenterX(FromOut->GetOwningNode());
		const int32 Y0 = NodeCenterY(FromOut->GetOwningNode());
		const int32 X1 = NodeCenterX(ToIn->GetOwningNode());
		const int32 Y1 = NodeCenterY(ToIn->GetOwningNode());
		const int32 Dist = FMath::Abs(X1 - X0) + FMath::Abs(Y1 - Y0);
		if (Dist < Threshold)
		{
			continue;
		}

		UK2Node_Knot* Knot = NewObject<UK2Node_Knot>(Graph);
		Graph->AddNode(Knot, true, false);
		Knot->NodePosX = (X0 + X1) / 2 - 32;
		Knot->NodePosY = (Y0 + Y1) / 2 - 16;
		Knot->CreateNewGuid();
		Knot->PostPlacedNewNode();
		Knot->AllocateDefaultPins();

		UEdGraphPin* KnotIn = Knot->GetInputPin();
		UEdGraphPin* KnotOut = Knot->GetOutputPin();
		if (!KnotIn || !KnotOut)
		{
			Graph->RemoveNode(Knot);
			continue;
		}

		Schema->BreakSinglePinLink(FromOut, ToIn);
		if (!Schema->TryCreateConnection(FromOut, KnotIn))
		{
			// Restore best-effort
			Schema->TryCreateConnection(FromOut, ToIn);
			Graph->RemoveNode(Knot);
			continue;
		}
		if (!Schema->TryCreateConnection(KnotOut, ToIn))
		{
			Schema->BreakSinglePinLink(FromOut, KnotIn);
			Schema->TryCreateConnection(FromOut, ToIn);
			Graph->RemoveNode(Knot);
			continue;
		}
		++KnotsAdded;
	}

	return KnotsAdded;
}
