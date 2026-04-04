// SPDX-License-Identifier: MIT

#include "BlueprintFormat/BlueprintGraphLayoutAnalysis.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MultiGate.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"

namespace UnrealBlueprintGraphLayoutAnalysisPriv
{
	static bool IsExecPin(const UEdGraphPin* P)
	{
		return P && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	static bool IsBranchLike(UEdGraphNode* N)
	{
		return Cast<UK2Node_IfThenElse>(N) || Cast<UK2Node_MultiGate>(N) || Cast<UK2Node_SwitchEnum>(N)
			|| Cast<UK2Node_SwitchInteger>(N) || Cast<UK2Node_SwitchName>(N) || Cast<UK2Node_SwitchString>(N);
	}

	static int32 CountDataLinkCrossings(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return 0;
		}
		struct FSeg
		{
			int32 X0, Y0, X1, Y1;
		};
		TArray<FSeg> Segs;
		auto Center = [](UEdGraphNode* N) -> FIntPoint
		{
			if (!N)
			{
				return FIntPoint::ZeroValue;
			}
			const int32 W = FMath::Max(64, N->NodeWidth > 0 ? N->NodeWidth : 240);
			const int32 H = FMath::Max(32, N->NodeHeight > 0 ? N->NodeHeight : 120);
			return FIntPoint(N->NodePosX + W / 2, N->NodePosY + H / 2);
		};
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
					const FIntPoint CA = Center(A);
					const FIntPoint CB = Center(B);
					if (LexToString(A->NodeGuid) > LexToString(B->NodeGuid))
					{
						continue;
					}
					Segs.Add({CA.X, CA.Y, CB.X, CB.Y});
				}
			}
		}
		if (Segs.Num() > 400)
		{
			return -1;
		}
		auto Cross = [](const FSeg& A, const FSeg& B) -> bool
		{
			const int64 O1 = (int64(B.X0) - A.X0) * (int64(B.Y1) - A.Y0) - (int64(B.Y0) - A.Y0) * (int64(B.X1) - A.X0);
			const int64 O2 = (int64(B.X1) - A.X0) * (int64(B.Y1) - A.Y0) - (int64(B.Y1) - A.Y0) * (int64(B.X1) - A.X0);
			const int64 O3 = (int64(A.X0) - B.X0) * (int64(A.Y1) - B.Y0) - (int64(A.Y0) - B.Y0) * (int64(A.X1) - B.X0);
			const int64 O4 = (int64(A.X1) - B.X0) * (int64(A.Y1) - B.Y0) - (int64(A.Y1) - B.Y0) * (int64(A.X1) - B.X0);
			return (O1 > 0) != (O2 > 0) && (O3 > 0) != (O4 > 0);
		};
		int32 C = 0;
		for (int32 i = 0; i < Segs.Num(); ++i)
		{
			for (int32 j = i + 1; j < Segs.Num(); ++j)
			{
				if (Cross(Segs[i], Segs[j]))
				{
					++C;
				}
			}
		}
		return C;
	}
}

void UnrealBlueprintGraphLayoutAnalysis::AppendToJsonObject(UEdGraph* Graph, TSharedPtr<FJsonObject> Target)
{
	using namespace UnrealBlueprintGraphLayoutAnalysisPriv;
	if (!Graph || !Target.IsValid())
	{
		return;
	}
	int32 ScriptNodes = 0;
	int32 Comments = 0;
	int32 Entries = 0;
	int32 Branches = 0;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N)
		{
			continue;
		}
		if (Cast<UEdGraphNode_Comment>(N))
		{
			++Comments;
			continue;
		}
		++ScriptNodes;
		if (Cast<UK2Node_Event>(N) || Cast<UK2Node_CustomEvent>(N) || Cast<UK2Node_FunctionEntry>(N))
		{
			++Entries;
		}
		if (IsBranchLike(N))
		{
			++Branches;
		}
	}
	Target->SetNumberField(TEXT("script_node_count"), static_cast<double>(ScriptNodes));
	Target->SetNumberField(TEXT("comment_node_count"), static_cast<double>(Comments));
	Target->SetNumberField(TEXT("entry_node_count"), static_cast<double>(Entries));
	Target->SetNumberField(TEXT("branch_like_node_count"), static_cast<double>(Branches));
	const int32 X = CountDataLinkCrossings(Graph);
	if (X >= 0)
	{
		Target->SetNumberField(TEXT("data_link_crossing_proxy"), static_cast<double>(X));
	}
	else
	{
		Target->SetStringField(TEXT("data_link_crossing_proxy"), TEXT("skipped_too_many_links"));
	}
}
