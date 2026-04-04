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

	static int64 Orient2D(int32 Ax, int32 Ay, int32 Bx, int32 By, int32 Cx, int32 Cy)
	{
		return (int64(Bx) - Ax) * (int64(Cy) - Ay) - (int64(By) - Ay) * (int64(Cx) - Ax);
	}

	static bool OnSegment(int32 Ax, int32 Ay, int32 Bx, int32 By, int32 Cx, int32 Cy)
	{
		return FMath::Min(Ax, Bx) <= Cx && Cx <= FMath::Max(Ax, Bx) && FMath::Min(Ay, By) <= Cy
			&& Cy <= FMath::Max(Ay, By);
	}

	static bool SegmentsIntersect(int32 Ax, int32 Ay, int32 Bx, int32 By, int32 Cx, int32 Cy, int32 Dx, int32 Dy)
	{
		const int64 O1 = Orient2D(Ax, Ay, Bx, By, Cx, Cy);
		const int64 O2 = Orient2D(Ax, Ay, Bx, By, Dx, Dy);
		const int64 O3 = Orient2D(Cx, Cy, Dx, Dy, Ax, Ay);
		const int64 O4 = Orient2D(Cx, Cy, Dx, Dy, Bx, By);
		if (O1 == 0 && OnSegment(Ax, Ay, Bx, By, Cx, Cy))
		{
			return true;
		}
		if (O2 == 0 && OnSegment(Ax, Ay, Bx, By, Dx, Dy))
		{
			return true;
		}
		if (O3 == 0 && OnSegment(Cx, Cy, Dx, Dy, Ax, Ay))
		{
			return true;
		}
		if (O4 == 0 && OnSegment(Cx, Cy, Dx, Dy, Bx, By))
		{
			return true;
		}
		return (O1 > 0) != (O2 > 0) && (O3 > 0) != (O4 > 0);
	}

	static bool TryInsertOneKnot(
		UEdGraph* Graph,
		const UEdGraphSchema_K2* Schema,
		UEdGraphPin* FromOut,
		UEdGraphPin* ToIn,
		int32& KnotsAdded)
	{
		if (!Graph || !Schema || !FromOut || !ToIn)
		{
			return false;
		}
		const int32 X0 = NodeCenterX(FromOut->GetOwningNode());
		const int32 Y0 = NodeCenterY(FromOut->GetOwningNode());
		const int32 X1 = NodeCenterX(ToIn->GetOwningNode());
		const int32 Y1 = NodeCenterY(ToIn->GetOwningNode());

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
			return false;
		}

		Schema->BreakSinglePinLink(FromOut, ToIn);
		if (!Schema->TryCreateConnection(FromOut, KnotIn))
		{
			Schema->TryCreateConnection(FromOut, ToIn);
			Graph->RemoveNode(Knot);
			return false;
		}
		if (!Schema->TryCreateConnection(KnotOut, ToIn))
		{
			Schema->BreakSinglePinLink(FromOut, KnotIn);
			Schema->TryCreateConnection(FromOut, ToIn);
			Graph->RemoveNode(Knot);
			return false;
		}
		++KnotsAdded;
		return true;
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
		TryInsertOneKnot(Graph, Schema, FromOut, ToIn, KnotsAdded);
	}

	return KnotsAdded;
}

int32 UnrealBlueprintKnotService::ApplyWireKnots(UEdGraph* Graph, const FUnrealBlueprintGraphFormatOptions& Options)
{
	using namespace UnrealBlueprintKnotServicePriv;
	if (!Graph)
	{
		return 0;
	}

	int32 Threshold = TNumericLimits<int32>::Max();
	if (Options.WireKnotAggression != EUnrealBlueprintWireKnotAggression::Off)
	{
		Threshold = FMath::Min(Threshold, DistanceThreshold(Options.WireKnotAggression));
	}
	if (Options.MaxWireLengthBeforeReroute > 0)
	{
		Threshold = FMath::Min(Threshold, Options.MaxWireLengthBeforeReroute);
	}
	const bool bLengthKnots = Threshold < TNumericLimits<int32>::Max() / 4;
	const bool bCrossingKnots = Options.MaxCrossingsPerSegment > 0;

	if (!bLengthKnots && !bCrossingKnots)
	{
		return 0;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	int32 KnotsAdded = 0;

	struct FLinkRec
	{
		UEdGraphPin* From = nullptr;
		UEdGraphPin* To = nullptr;
		int32 X0 = 0;
		int32 Y0 = 0;
		int32 X1 = 0;
		int32 Y1 = 0;
		int32 Dist = 0;
	};
	TArray<FLinkRec> Links;
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
				FLinkRec R;
				R.From = Pin;
				R.To = Other;
				R.X0 = NodeCenterX(A);
				R.Y0 = NodeCenterY(A);
				R.X1 = NodeCenterX(B);
				R.Y1 = NodeCenterY(B);
				R.Dist = FMath::Abs(R.X1 - R.X0) + FMath::Abs(R.Y1 - R.Y0);
				Links.Add(R);
			}
		}
	}

	const int32 MaxLinksForCrossing = 400;
	const bool bDoCross = bCrossingKnots && Links.Num() <= MaxLinksForCrossing;

	for (int32 i = 0; i < Links.Num(); ++i)
	{
		const FLinkRec& L = Links[i];
		if (!L.From || !L.To)
		{
			continue;
		}
		bool bWant = false;
		if (bLengthKnots && L.Dist >= Threshold)
		{
			bWant = true;
		}
		if (bDoCross)
		{
			int32 Cross = 0;
			for (int32 j = 0; j < Links.Num(); ++j)
			{
				if (i == j)
				{
					continue;
				}
				const FLinkRec& M = Links[j];
				if (SegmentsIntersect(L.X0, L.Y0, L.X1, L.Y1, M.X0, M.Y0, M.X1, M.Y1))
				{
					++Cross;
				}
			}
			if (Cross >= Options.MaxCrossingsPerSegment)
			{
				bWant = true;
			}
		}
		if (!bWant)
		{
			continue;
		}
		TryInsertOneKnot(Graph, Schema, L.From, L.To, KnotsAdded);
	}

	return KnotsAdded;
}
