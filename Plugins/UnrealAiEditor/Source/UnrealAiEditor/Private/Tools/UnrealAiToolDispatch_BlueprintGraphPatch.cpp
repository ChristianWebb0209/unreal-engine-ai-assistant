#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

#include "Tools/UnrealAiToolDispatch_ArgRepair.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/Presentation/UnrealAiToolEditorNoteBuilders.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphNode_Comment.h"
#include "Misc/Guid.h"
#include "ScopedTransaction.h"
#include "BlueprintFormat/BlueprintGraphCommentReflow.h"
#include "BlueprintFormat/BlueprintGraphFormatService.h"
#include "BlueprintFormat/UnrealAiBlueprintFormatterBridge.h"
#include "UnrealAiEditorSettings.h"
#include "Tools/UnrealAiBlueprintFunctionResolve.h"
#include "Tools/UnrealAiBlueprintGraphNodeGuid.h"
#include "Tools/UnrealAiToolProjectPathAllowlist.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonValue.h"
#include "Containers/Queue.h"
#include "UObject/Class.h"

namespace UnrealAiBlueprintGraphPatchPriv
{
	static FString InferBlueprintGraphPatchErrorCode(const FString& Msg);
	static void AppendFailedOpSnippetToPayload(
		const TArray<TSharedPtr<FJsonValue>>* OpsArr,
		int32 FailedIdx,
		const TSharedPtr<FJsonObject>& Payload);
	static void AppendBlueprintGraphPatchSuggestedCorrectCall(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& FirstError,
		const TSharedPtr<FJsonObject>& Payload);

	/** Max nodes in union for patched_nodes_and_downstream_exec (includes seeds). */
	static constexpr int32 LayoutDownstreamMaxTotalNodes = 384;
	static constexpr int32 LayoutBelowExistingMarginY = 200;

	static bool GraphPatchTakesPartInScriptLayout(const UEdGraphNode* N)
	{
		return N && !Cast<UEdGraphNode_Comment>(N);
	}

	static bool GraphPatchIsExecPin(const UEdGraphPin* P)
	{
		return P && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	/** Match BlueprintGraphLayeredDagLayout exec successor ordering (sorted output exec pins). */
	static TArray<UEdGraphNode*> GraphPatchExecSuccessorsOrdered(UEdGraphNode* Node)
	{
		TArray<UEdGraphNode*> Succ;
		if (!Node)
		{
			return Succ;
		}
		TArray<UEdGraphPin*> OutExec;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && GraphPatchIsExecPin(Pin) && !Pin->bHidden)
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

	static int32 CountScriptLayoutNodesIn(const TArray<UEdGraphNode*>& Nodes)
	{
		int32 C = 0;
		for (UEdGraphNode* N : Nodes)
		{
			if (GraphPatchTakesPartInScriptLayout(N))
			{
				++C;
			}
		}
		return C;
	}

	/**
	 * BFS along exec outputs within Graph; adds downstream nodes to InOutLayoutNodes.
	 * Caps total unique script nodes at LayoutDownstreamMaxTotalNodes.
	 */
	static void ExpandLayoutNodesWithDownstreamExec(
		UEdGraph* Graph,
		TArray<UEdGraphNode*>& InOutLayoutNodes,
		bool& bOutTruncated,
		int32& OutAddedCount)
	{
		bOutTruncated = false;
		OutAddedCount = 0;
		if (!Graph)
		{
			return;
		}

		TSet<UEdGraphNode*> Seen;
		TQueue<UEdGraphNode*> Q;
		for (UEdGraphNode* Seed : InOutLayoutNodes)
		{
			if (!GraphPatchTakesPartInScriptLayout(Seed) || Seed->GetGraph() != Graph)
			{
				continue;
			}
			if (!Seen.Contains(Seed))
			{
				Seen.Add(Seed);
				Q.Enqueue(Seed);
			}
		}

		while (!Q.IsEmpty())
		{
			if (Seen.Num() >= LayoutDownstreamMaxTotalNodes)
			{
				bOutTruncated = true;
				break;
			}
			UEdGraphNode* N = nullptr;
			Q.Dequeue(N);
			if (!N)
			{
				continue;
			}
			for (UEdGraphNode* Succ : GraphPatchExecSuccessorsOrdered(N))
			{
				if (!GraphPatchTakesPartInScriptLayout(Succ) || Succ->GetGraph() != Graph)
				{
					continue;
				}
				if (Seen.Contains(Succ))
				{
					continue;
				}
				if (Seen.Num() >= LayoutDownstreamMaxTotalNodes)
				{
					bOutTruncated = true;
					break;
				}
				Seen.Add(Succ);
				Q.Enqueue(Succ);
				InOutLayoutNodes.AddUnique(Succ);
				++OutAddedCount;
			}
			if (bOutTruncated)
			{
				break;
			}
		}
	}

	/** Autonomix-style: shift materialized cluster down so it sits below existing script nodes. */
	static void ApplyBelowExistingVerticalAnchor(UEdGraph* Graph, const TArray<UEdGraphNode*>& Materialized, int32 MarginY)
	{
		if (!Graph || Materialized.Num() == 0)
		{
			return;
		}
		TSet<const UEdGraphNode*> M;
		for (UEdGraphNode* N : Materialized)
		{
			if (N)
			{
				M.Add(N);
			}
		}
		int32 MaxBottomY = MIN_int32;
		for (UEdGraphNode* U : Graph->Nodes)
		{
			if (!U || M.Contains(U) || !GraphPatchTakesPartInScriptLayout(U))
			{
				continue;
			}
			const int32 H = FMath::Max(32, U->NodeHeight > 0 ? U->NodeHeight : 120);
			MaxBottomY = FMath::Max(MaxBottomY, U->NodePosY + H);
		}
		if (MaxBottomY == MIN_int32)
		{
			return;
		}
		int32 MinMatY = MAX_int32;
		for (UEdGraphNode* N : Materialized)
		{
			if (!N)
			{
				continue;
			}
			MinMatY = FMath::Min(MinMatY, N->NodePosY);
		}
		if (MinMatY == MAX_int32)
		{
			return;
		}
		const int32 TargetTop = MaxBottomY + MarginY;
		const int32 DY = TargetTop - MinMatY;
		if (DY <= 0)
		{
			return;
		}
		for (UEdGraphNode* N : Materialized)
		{
			if (N)
			{
				N->NodePosY += DY;
				N->Modify();
			}
		}
	}

	struct FCommentReflowJob
	{
		UEdGraphNode_Comment* Comment = nullptr;
		TArray<FString> MemberNodeParts;
	};

	static bool TryBreakPinLink(UEdGraphPin* PA, UEdGraphPin* PB, const UEdGraphSchema* Schema)
	{
		if (!PA || !PB || !Schema)
		{
			return false;
		}
		for (UEdGraphPin* L : PA->LinkedTo)
		{
			if (L == PB)
			{
				Schema->BreakSinglePinLink(PA, PB);
				return true;
			}
		}
		for (UEdGraphPin* L : PB->LinkedTo)
		{
			if (L == PA)
			{
				Schema->BreakSinglePinLink(PB, PA);
				return true;
			}
		}
		return false;
	}

	static FString PinDebugSummary(const UEdGraphPin* P)
	{
		if (!P)
		{
			return TEXT("null");
		}
		const TCHAR* Dir = (P->Direction == EGPD_Input) ? TEXT("in") : TEXT("out");
		return FString::Printf(TEXT("%s dir=%s cat=%s"), *P->PinName.ToString(), Dir, *P->PinType.PinCategory.ToString());
	}

	/** K2: output pin -> input pin only (no silent reversal). */
	static bool TryCreateDirectedLink(UEdGraphPin* FromOutput, UEdGraphPin* ToInput, const UEdGraphSchema_K2* Schema, FString* OutErr)
	{
		if (!FromOutput || !ToInput || !Schema)
		{
			return false;
		}
		if (FromOutput->Direction != EGPD_Output || ToInput->Direction != EGPD_Input)
		{
			if (OutErr)
			{
				*OutErr = FString::Printf(
					TEXT("connect requires from=output and to=input; got %s -> %s (swap from/to or fix pin names)"),
					*PinDebugSummary(FromOutput),
					*PinDebugSummary(ToInput));
			}
			return false;
		}
		if (!Schema->TryCreateConnection(FromOutput, ToInput))
		{
			if (OutErr)
			{
				*OutErr = FString::Printf(
					TEXT("TryCreateConnection failed: %s -> %s"),
					*PinDebugSummary(FromOutput),
					*PinDebugSummary(ToInput));
			}
			return false;
		}
		return true;
	}

	static void SeedInsertedNodeBetweenNeighbors(UEdGraphNode* Mid, const UEdGraphNode* Up, const UEdGraphNode* Down)
	{
		if (!Mid || !Up || !Down)
		{
			return;
		}
		auto Bounds = [](const UEdGraphNode* N)
		{
			struct FR
			{
				int32 MinX = 0;
				int32 MinY = 0;
				int32 MaxX = 0;
				int32 MaxY = 0;
			} R;
			const int32 W = FMath::Max(64, N->NodeWidth > 0 ? N->NodeWidth : 240);
			const int32 H = FMath::Max(32, N->NodeHeight > 0 ? N->NodeHeight : 120);
			R.MinX = N->NodePosX;
			R.MinY = N->NodePosY;
			R.MaxX = R.MinX + W;
			R.MaxY = R.MinY + H;
			return R;
		};
		const auto A = Bounds(Up);
		const auto B = Bounds(Down);
		const int32 MidW = FMath::Max(64, Mid->NodeWidth > 0 ? Mid->NodeWidth : 240);
		const int32 MidH = FMath::Max(32, Mid->NodeHeight > 0 ? Mid->NodeHeight : 120);
		const int32 Cx = ((A.MinX + A.MaxX + B.MinX + B.MaxX) / 4) - MidW / 2;
		const int32 Cy = ((A.MinY + A.MaxY + B.MinY + B.MaxY) / 4) - MidH / 2;
		Mid->NodePosX = Cx;
		Mid->NodePosY = Cy;
	}

	static FString NormalizePinToken(const FString& PinName)
	{
		FString P = PinName;
		P.TrimStartAndEndInline();
		if (P.IsEmpty())
		{
			return P;
		}
		if (P.Equals(TEXT("Exec"), ESearchCase::IgnoreCase) || P.Equals(TEXT("Execute"), ESearchCase::IgnoreCase)
			|| P.Equals(TEXT("Execution"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Execute.ToString();
		}
		if (P.Equals(TEXT("Then"), ESearchCase::IgnoreCase) || P.Equals(TEXT("True"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Then.ToString();
		}
		if (P.Equals(TEXT("Else"), ESearchCase::IgnoreCase) || P.Equals(TEXT("False"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Else.ToString();
		}
		if (P.Equals(TEXT("Condition"), ESearchCase::IgnoreCase))
		{
			return TEXT("Condition");
		}
		return P;
	}

	static bool SplitNodePinRef(const FString& Ref, FString& OutNodePart, FString& OutPin)
	{
		int32 Dot = INDEX_NONE;
		if (!Ref.FindLastChar(TEXT('.'), Dot) || Dot <= 0 || Dot >= Ref.Len() - 1)
		{
			return false;
		}
		OutNodePart = Ref.Left(Dot);
		OutPin = Ref.Mid(Dot + 1);
		OutNodePart.TrimStartAndEndInline();
		OutPin.TrimStartAndEndInline();
		return !OutNodePart.IsEmpty() && !OutPin.IsEmpty();
	}

	static UEdGraphNode* FindNodeByGraphGuid(UEdGraph* Graph, const FGuid& G)
	{
		for (UEdGraphNode* N : Graph->Nodes)
		{
			if (N && N->NodeGuid == G)
			{
				return N;
			}
		}
		return nullptr;
	}

	static void FinishGuidMissErr(
		UBlueprint* GuidHintBlueprint,
		const FGuid& G,
		TArray<FString>* OutFoundInOtherGraphs,
		FString& Err,
		const FString& RefForErr)
	{
		TArray<FString> LocalNames;
		TArray<FString>& Names = OutFoundInOtherGraphs ? *OutFoundInOtherGraphs : LocalNames;
		if (OutFoundInOtherGraphs)
		{
			OutFoundInOtherGraphs->Reset();
		}
		else
		{
			LocalNames.Reset();
		}
		if (GuidHintBlueprint)
		{
			UnrealAiBlueprintTools_FindGraphNamesContainingNodeGuid(GuidHintBlueprint, G, Names);
		}
		if (Names.Num() > 0)
		{
			Err = FString::Printf(
				TEXT("Node guid not found in graph: %s. Same node_guid exists in graph(s): %s. Use \"graph_name\" with one of these in blueprint_graph_patch, or blueprint_graph_list_pins (cross-graph resolution)."),
				*RefForErr,
				*FString::Join(Names, TEXT(", ")));
			return;
		}
		Err = FString::Printf(TEXT("Node guid not found in graph: %s"), *RefForErr);
	}

	static bool PatchExtractVariableTypeString(const TSharedPtr<FJsonObject>& Op, FString& OutType)
	{
		OutType.Reset();
		auto CoerceFromObject = [&](const TSharedPtr<FJsonObject>& O) -> bool
		{
			if (!O.IsValid())
			{
				return false;
			}
			FString Cat, Sub, SubObj;
			O->TryGetStringField(TEXT("category"), Cat);
			if (Cat.IsEmpty())
			{
				O->TryGetStringField(TEXT("pin_category"), Cat);
			}
			O->TryGetStringField(TEXT("subcategory"), Sub);
			if (Sub.IsEmpty())
			{
				O->TryGetStringField(TEXT("pin_subcategory"), Sub);
			}
			O->TryGetStringField(TEXT("subcategory_object"), SubObj);
			if (SubObj.IsEmpty())
			{
				O->TryGetStringField(TEXT("class_path"), SubObj);
			}
			Cat.TrimStartAndEndInline();
			Sub.TrimStartAndEndInline();
			SubObj.TrimStartAndEndInline();
			const FString CL = Cat.ToLower();
			const FString SL = Sub.ToLower();
			if (!SubObj.IsEmpty()
				&& (CL == TEXT("object") || CL == TEXT("class") || CL == TEXT("softobject") || CL == TEXT("softclass")))
			{
				OutType = SubObj;
				return true;
			}
			if (CL == TEXT("struct") && !SubObj.IsEmpty())
			{
				OutType = SubObj;
				return true;
			}
			if (!Cat.IsEmpty() && !Sub.IsEmpty())
			{
				if (CL == TEXT("real") || CL == TEXT("float") || CL == TEXT("double"))
				{
					OutType = (SL == TEXT("double")) ? TEXT("double") : TEXT("float");
					return true;
				}
				if (CL == TEXT("int"))
				{
					OutType = TEXT("int");
					return true;
				}
			}
			if (!Cat.IsEmpty())
			{
				OutType = Cat;
				return true;
			}
			return false;
		};

		if (Op->TryGetStringField(TEXT("type"), OutType) && !OutType.IsEmpty())
		{
			return true;
		}
		if (Op->TryGetStringField(TEXT("variable_type"), OutType) && !OutType.IsEmpty())
		{
			return true;
		}
		const TSharedPtr<FJsonObject>* AsObj = nullptr;
		if (Op->TryGetObjectField(TEXT("type"), AsObj) && AsObj && CoerceFromObject(*AsObj))
		{
			return true;
		}
		if (Op->TryGetObjectField(TEXT("variable_type"), AsObj) && AsObj && CoerceFromObject(*AsObj))
		{
			return true;
		}
		return false;
	}

	static UEdGraphNode* ResolveNodePart(
		const FString& NodePartIn,
		const TMap<FString, UEdGraphNode*>& PatchMap,
		UEdGraph* Graph,
		FString& Err,
		UBlueprint* GuidHintBlueprint = nullptr,
		TArray<FString>* OutFoundInOtherGraphs = nullptr)
	{
		if (OutFoundInOtherGraphs)
		{
			OutFoundInOtherGraphs->Reset();
		}
		FString NodePart = NodePartIn;
		NodePart.TrimStartAndEndInline();
		if (NodePart.Contains(TEXT("__UAI_G_")))
		{
			Err = FString::Printf(
				TEXT("Legacy __UAI_G_* placeholder in node ref \"%s\" is not valid in blueprint_graph_patch. "
					 "Use guid:<uuid> from blueprint_graph_introspect, or patch_id from this ops[] batch."),
				*NodePartIn);
			return nullptr;
		}
		if (NodePart.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
		{
			const FString GuidBody = NodePart.Mid(5).TrimStartAndEnd();
			if (GuidBody.Contains(TEXT("__UAI_G_")))
			{
				Err = FString::Printf(
					TEXT("Legacy __UAI_G_* in graph_patch node ref: %s. Use guid:<real-uuid> from blueprint_graph_introspect, or patch_id from this ops[] batch."),
					*NodePart);
				return nullptr;
			}
			FGuid G;
			if (!UnrealAiTryParseBlueprintGraphNodeGuid(NodePart, G, nullptr))
			{
				Err = FString::Printf(TEXT("Invalid guid ref: %s"), *NodePart);
				return nullptr;
			}
			if (UEdGraphNode* Found = FindNodeByGraphGuid(Graph, G))
			{
				return Found;
			}
			FinishGuidMissErr(GuidHintBlueprint, G, OutFoundInOtherGraphs, Err, NodePart);
			return nullptr;
		}
		if (UEdGraphNode* const* Found = PatchMap.Find(NodePart))
		{
			return *Found;
		}
		FGuid BareGuid;
		if (UnrealAiTryParseBlueprintGraphNodeGuid(NodePart, BareGuid, nullptr))
		{
			if (UEdGraphNode* GNode = FindNodeByGraphGuid(Graph, BareGuid))
			{
				return GNode;
			}
			FinishGuidMissErr(GuidHintBlueprint, BareGuid, OutFoundInOtherGraphs, Err, NodePart);
			return nullptr;
		}
		Err = FString::Printf(
			TEXT("Unknown node ref \"%s\" — use a patch_id from this batch, guid:..., or node_guid from blueprint_graph_introspect"),
			*NodePart);
		return nullptr;
	}

	/** validate_only: resolve patch_id to transient batch node, else guid/bare-guid on the graph. */
	static bool TryResolveNodePartValidateOnly(
		const FString& NodePartIn,
		const TMap<FString, UEdGraphNode*>& VirtualPatchNodes,
		UEdGraph* Graph,
		UEdGraphNode*& OutNode,
		FString& Err,
		UBlueprint* GuidHintBlueprint = nullptr,
		TArray<FString>* OutFoundInOtherGraphs = nullptr)
	{
		OutNode = nullptr;
		if (OutFoundInOtherGraphs)
		{
			OutFoundInOtherGraphs->Reset();
		}
		FString NodePart = NodePartIn;
		NodePart.TrimStartAndEndInline();
		if (NodePart.Contains(TEXT("__UAI_G_")))
		{
			Err = FString::Printf(
				TEXT("Legacy __UAI_G_* placeholder in node ref \"%s\" is not valid in blueprint_graph_patch. "
					 "Use guid:<uuid> from blueprint_graph_introspect, or patch_id from this ops[] batch."),
				*NodePartIn);
			return false;
		}
		if (UEdGraphNode* const* PatchNode = VirtualPatchNodes.Find(NodePart))
		{
			OutNode = *PatchNode;
			return true;
		}
		if (NodePart.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
		{
			const FString GuidBody = NodePart.Mid(5).TrimStartAndEnd();
			if (GuidBody.Contains(TEXT("__UAI_G_")))
			{
				Err = FString::Printf(
					TEXT("Legacy __UAI_G_* in graph_patch node ref: %s. Use guid:<real-uuid> from blueprint_graph_introspect, or patch_id from this ops[] batch."),
					*NodePart);
				return false;
			}
			FGuid G;
			if (!UnrealAiTryParseBlueprintGraphNodeGuid(NodePart, G, nullptr))
			{
				Err = FString::Printf(TEXT("Invalid guid ref: %s"), *NodePart);
				return false;
			}
			if (UEdGraphNode* Found = FindNodeByGraphGuid(Graph, G))
			{
				OutNode = Found;
				return true;
			}
			FinishGuidMissErr(GuidHintBlueprint, G, OutFoundInOtherGraphs, Err, NodePart);
			return false;
		}
		FGuid BareGuid;
		if (UnrealAiTryParseBlueprintGraphNodeGuid(NodePart, BareGuid, nullptr))
		{
			if (UEdGraphNode* GNode = FindNodeByGraphGuid(Graph, BareGuid))
			{
				OutNode = GNode;
				return true;
			}
			FinishGuidMissErr(GuidHintBlueprint, BareGuid, OutFoundInOtherGraphs, Err, NodePart);
			return false;
		}
		Err = FString::Printf(
			TEXT("Unknown node ref \"%s\" — declare create_node with this patch_id earlier in ops[], or use guid:... / node_guid"),
			*NodePart);
		return false;
	}

	static bool PinsAreDirectlyLinked(UEdGraphPin* PA, UEdGraphPin* PB)
	{
		if (!PA || !PB)
		{
			return false;
		}
		for (UEdGraphPin* L : PA->LinkedTo)
		{
			if (L == PB)
			{
				return true;
			}
		}
		return false;
	}

	static UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, FString* OutError = nullptr)
	{
		if (!Node || PinName.IsEmpty())
		{
			return nullptr;
		}
		if (OutError)
		{
			OutError->Reset();
		}
		const FString Nrm = NormalizePinToken(PinName);

		int32 PinNameMatches = 0;
		UEdGraphPin* PinNameHit = nullptr;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (!P || P->bHidden)
			{
				continue;
			}
			if (P->PinName.ToString().Equals(Nrm, ESearchCase::IgnoreCase)
				|| P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				++PinNameMatches;
				if (!PinNameHit)
				{
					PinNameHit = P;
				}
			}
		}
		if (PinNameMatches > 1)
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("ambiguous pin name \"%s\" (%d visible matches)"), *PinName, PinNameMatches);
			}
			return nullptr;
		}
		if (PinNameMatches == 1)
		{
			return PinNameHit;
		}

		TArray<UEdGraphPin*> DispMatches;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (!P || P->bHidden)
			{
				continue;
			}
			const FString Dn = P->GetDisplayName().ToString();
			if (Dn.Equals(PinName, ESearchCase::IgnoreCase) || Dn.Equals(Nrm, ESearchCase::IgnoreCase))
			{
				DispMatches.Add(P);
			}
		}
		if (DispMatches.Num() > 1)
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("ambiguous pin display \"%s\" (%d matches)"), *PinName, DispMatches.Num());
			}
			return nullptr;
		}
		if (DispMatches.Num() == 1)
		{
			return DispMatches[0];
		}

		auto TryAliasPinName = [&](const TCHAR* Alias) -> UEdGraphPin*
		{
			for (UEdGraphPin* P : Node->Pins)
			{
				if (P && P->PinName.ToString().Equals(Alias, ESearchCase::IgnoreCase))
				{
					return P;
				}
			}
			return nullptr;
		};
		if (PinName.Equals(TEXT("inputObject"), ESearchCase::IgnoreCase))
		{
			if (UEdGraphPin* P = TryAliasPinName(TEXT("Target")))
			{
				return P;
			}
			if (UEdGraphPin* P = TryAliasPinName(TEXT("Self")))
			{
				return P;
			}
			if (UEdGraphPin* P = TryAliasPinName(TEXT("Object")))
			{
				return P;
			}
		}
		if (PinName.Equals(TEXT("output"), ESearchCase::IgnoreCase)
			|| PinName.Equals(TEXT("returnValue"), ESearchCase::IgnoreCase))
		{
			if (UEdGraphPin* P = TryAliasPinName(TEXT("ReturnValue")))
			{
				return P;
			}
		}
		auto IsExecCategory = [](const UEdGraphPin* P) -> bool
		{
			return P && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
		};
		if (UK2Node_VariableSet* VS = Cast<UK2Node_VariableSet>(Node))
		{
			if (PinName.Equals(TEXT("Set"), ESearchCase::IgnoreCase)
				|| PinName.Equals(TEXT("Value"), ESearchCase::IgnoreCase)
				|| PinName.Equals(TEXT("Input"), ESearchCase::IgnoreCase))
			{
				const FName VarFName = VS->VariableReference.GetMemberName();
				if (VarFName != NAME_None)
				{
					for (UEdGraphPin* P : Node->Pins)
					{
						if (!P || P->bHidden || P->Direction != EGPD_Input || IsExecCategory(P))
						{
							continue;
						}
						if (P->PinName == VarFName)
						{
							return P;
						}
					}
				}
				for (UEdGraphPin* P : Node->Pins)
				{
					if (!P || P->bHidden || P->Direction != EGPD_Input || IsExecCategory(P))
					{
						continue;
					}
					if (P->PinName == UEdGraphSchema_K2::PN_Self)
					{
						continue;
					}
					return P;
				}
			}
		}
		if (UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(Node))
		{
			if (PinName.Equals(TEXT("Get"), ESearchCase::IgnoreCase)
				|| PinName.Equals(TEXT("Value"), ESearchCase::IgnoreCase)
				|| PinName.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
			{
				const FName VarFName = VG->VariableReference.GetMemberName();
				if (VarFName != NAME_None)
				{
					for (UEdGraphPin* P : Node->Pins)
					{
						if (!P || P->bHidden || P->Direction != EGPD_Output || IsExecCategory(P))
						{
							continue;
						}
						if (P->PinName == VarFName)
						{
							return P;
						}
					}
				}
				for (UEdGraphPin* P : Node->Pins)
				{
					if (!P || P->bHidden || P->Direction != EGPD_Output || IsExecCategory(P))
					{
						continue;
					}
					return P;
				}
			}
		}
		return nullptr;
	}

	/** When k2_class is set it wins; otherwise semantic_kind maps to a canonical UK2Node class path. */
	static bool TryResolveCreateNodeK2ClassPath(const TSharedPtr<FJsonObject>& Op, FString& OutK2Path, TArray<FString>& OutErrors)
	{
		OutK2Path.Reset();
		Op->TryGetStringField(TEXT("k2_class"), OutK2Path);
		OutK2Path.TrimStartAndEndInline();
		if (!OutK2Path.IsEmpty())
		{
			return true;
		}
		FString Sk;
		Op->TryGetStringField(TEXT("semantic_kind"), Sk);
		Sk.TrimStartAndEndInline();
		if (Sk.IsEmpty())
		{
			OutErrors.Add(TEXT(
				"create_node requires k2_class or semantic_kind (branch, execution_sequence, call_library_function, variable_get, variable_set, event_override, custom_event, dynamic_cast)"));
			return false;
		}
		const FString L = Sk.ToLower();
		if (L == TEXT("branch"))
		{
			OutK2Path = TEXT("/Script/BlueprintGraph.K2Node_IfThenElse");
			return true;
		}
		if (L == TEXT("execution_sequence"))
		{
			OutK2Path = TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence");
			return true;
		}
		if (L == TEXT("call_library_function"))
		{
			OutK2Path = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
			return true;
		}
		if (L == TEXT("variable_get"))
		{
			OutK2Path = TEXT("/Script/BlueprintGraph.K2Node_VariableGet");
			return true;
		}
		if (L == TEXT("variable_set"))
		{
			OutK2Path = TEXT("/Script/BlueprintGraph.K2Node_VariableSet");
			return true;
		}
		if (L == TEXT("event_override") || L == TEXT("event"))
		{
			OutK2Path = TEXT("/Script/BlueprintGraph.K2Node_Event");
			return true;
		}
		if (L == TEXT("custom_event"))
		{
			OutK2Path = TEXT("/Script/BlueprintGraph.K2Node_CustomEvent");
			return true;
		}
		if (L == TEXT("dynamic_cast"))
		{
			OutK2Path = TEXT("/Script/BlueprintGraph.K2Node_DynamicCast");
			return true;
		}
		if (L == TEXT("set_pin_default"))
		{
			OutErrors.Add(TEXT(
				"semantic_kind cannot be set_pin_default on create_node — use top-level op \"set_pin_default\" with ref \"patch_id.pin\" (or rely on automatic argument repair)."));
			return false;
		}
		OutErrors.Add(FString::Printf(
			TEXT("Unknown semantic_kind '%s'. Use k2_class for uncommon UK2Node types, or a supported semantic_kind."),
			*Sk));
		return false;
	}

	static UK2Node* CreateNodeFromPatchOp(
		UBlueprint* BP,
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& Op,
		TArray<FString>& OutErrors)
	{
		FString PatchId;
		if (!Op->TryGetStringField(TEXT("patch_id"), PatchId) || PatchId.IsEmpty())
		{
			OutErrors.Add(TEXT("create_node requires patch_id"));
			return nullptr;
		}
		FString K2ClassPath;
		if (!TryResolveCreateNodeK2ClassPath(Op, K2ClassPath, OutErrors))
		{
			return nullptr;
		}
		UClass* NodeClass = LoadObject<UClass>(nullptr, *K2ClassPath);
		if (!NodeClass || !NodeClass->IsChildOf(UK2Node::StaticClass()))
		{
			FString Hint;
			const FString Low = K2ClassPath.ToLower();
			if (Low.Contains(TEXT("intless")) || Low.Contains(TEXT("intadd")) || Low.Contains(TEXT("less_int"))
				|| Low.Contains(TEXT("add_int")) || Low.Contains(TEXT("k2node_math")))
			{
				Hint = TEXT(
					" Integer math/compare nodes are usually K2Node_CallFunction: class_path /Script/Engine.KismetMathLibrary, function_name Less_IntInt | Add_IntInt | Greater_IntInt | EqualEqual_IntInt.");
			}
			else if (Low.Contains(TEXT("literal")))
			{
				Hint = TEXT(
					" Literal ints: K2Node_CallFunction with KismetSystemLibrary.MakeLiteralInt (not KismetMathLibrary), or set_pin_default on an int input.");
			}
			OutErrors.Add(FString::Printf(TEXT("k2_class not a UK2Node: %s.%s"), *K2ClassPath, *Hint));
			return nullptr;
		}

		int32 X = 0;
		int32 Y = 0;
		{
			double XD = 0, YD = 0;
			if (Op->TryGetNumberField(TEXT("x"), XD))
			{
				X = FMath::RoundToInt(XD);
			}
			if (Op->TryGetNumberField(TEXT("y"), YD))
			{
				Y = FMath::RoundToInt(YD);
			}
		}

		UK2Node* Node = NewObject<UK2Node>(Graph, NodeClass);
		Graph->AddNode(Node, true, false);
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Node->CreateNewGuid();

		const TSharedPtr<FJsonObject>* CFObj = nullptr;
		if (Op->TryGetObjectField(TEXT("call_function"), CFObj) && CFObj && (*CFObj).IsValid())
		{
			UK2Node_CallFunction* CF = Cast<UK2Node_CallFunction>(Node);
			if (!CF)
			{
				OutErrors.Add(TEXT("call_function init requires K2Node_CallFunction k2_class"));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			FString ClsPath, FnName;
			(*CFObj)->TryGetStringField(TEXT("class_path"), ClsPath);
			(*CFObj)->TryGetStringField(TEXT("function_name"), FnName);
			UnrealAiBlueprintFunctionResolve::SplitCombinedClassPathAndFunctionName(ClsPath, FnName);
			UClass* Cls = ClsPath.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *ClsPath);
			if (!Cls || FnName.IsEmpty())
			{
				OutErrors.Add(TEXT("call_function requires class_path and function_name (split class::func if merged)."));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			UFunction* Fn = UnrealAiBlueprintFunctionResolve::ResolveCallFunction(Cls, FnName);
			if (!Fn)
			{
				FString Extra;
				const FString FnLow = FnName.ToLower();
				if (FnLow == TEXT("jump") || FnLow == TEXT("landed"))
				{
					Extra = TEXT(" Character movement: use class_path /Script/Engine.Character (not CharacterMovementComponent) for Jump/Landed.");
				}
				OutErrors.Add(FString::Printf(
					TEXT("Function not found on %s (after resolving name '%s'). Use class_path of the declaring UClass (e.g. /Script/Engine.Actor + GetActorLocation; /Script/Engine.KismetMathLibrary + RandomFloatInRange; /Script/Engine.KismetSystemLibrary + Delay; MakeLiteralInt/Float/Bool on KismetSystemLibrary).%s"),
					*ClsPath,
					*FnName,
					*Extra));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			CF->SetFromFunction(Fn);
			CF->PostPlacedNewNode();
			CF->AllocateDefaultPins();
		}
		else if (UK2Node_CallFunction* CF = Cast<UK2Node_CallFunction>(Node))
		{
			// Models often pass class_path + function_name at the top level (not only inside call_function).
			FString ClsPath, FnName;
			Op->TryGetStringField(TEXT("class_path"), ClsPath);
			Op->TryGetStringField(TEXT("function_name"), FnName);
			ClsPath.TrimStartAndEndInline();
			FnName.TrimStartAndEndInline();
			UnrealAiBlueprintFunctionResolve::SplitCombinedClassPathAndFunctionName(ClsPath, FnName);
			if (ClsPath.IsEmpty() || FnName.IsEmpty())
			{
				OutErrors.Add(TEXT(
					"K2Node_CallFunction requires class_path + function_name on the op (top-level) or call_function { class_path, function_name }."));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			UClass* Cls = LoadObject<UClass>(nullptr, *ClsPath);
			if (!Cls)
			{
				OutErrors.Add(FString::Printf(TEXT("class_path not found: %s"), *ClsPath));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			UFunction* Fn = UnrealAiBlueprintFunctionResolve::ResolveCallFunction(Cls, FnName);
			if (!Fn)
			{
				FString Extra;
				const FString FnLow = FnName.ToLower();
				if (FnLow == TEXT("jump") || FnLow == TEXT("landed"))
				{
					Extra = TEXT(" Character movement: use class_path /Script/Engine.Character (not CharacterMovementComponent) for Jump/Landed.");
				}
				OutErrors.Add(FString::Printf(
					TEXT("Function not found on %s (after resolving name '%s'). See KismetMathLibrary for math, Actor for GetActorLocation, KismetSystemLibrary for Delay/PrintString/MakeLiteral*.%s"),
					*ClsPath,
					*FnName,
					*Extra));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			CF->SetFromFunction(Fn);
			CF->PostPlacedNewNode();
			CF->AllocateDefaultPins();
		}
		else if (UK2Node_CustomEvent* Ce = Cast<UK2Node_CustomEvent>(Node))
		{
			FString EvName;
			Op->TryGetStringField(TEXT("event_name"), EvName);
			if (EvName.IsEmpty())
			{
				Op->TryGetStringField(TEXT("custom_event_name"), EvName);
			}
			const TSharedPtr<FJsonObject>* CEObj = nullptr;
			if (EvName.IsEmpty() && Op->TryGetObjectField(TEXT("custom_event"), CEObj) && CEObj && (*CEObj).IsValid())
			{
				(*CEObj)->TryGetStringField(TEXT("name"), EvName);
				if (EvName.IsEmpty())
				{
					(*CEObj)->TryGetStringField(TEXT("event_name"), EvName);
				}
			}
			if (EvName.IsEmpty())
			{
				Op->TryGetStringField(TEXT("name"), EvName);
			}
			if (EvName.IsEmpty())
			{
				OutErrors.Add(TEXT(
					"K2Node_CustomEvent needs event_name (or custom_event.name). User-defined events use k2_class /Script/BlueprintGraph.K2Node_CustomEvent — not K2Node_Event."));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			Ce->CustomFunctionName = FName(*EvName);
			Ce->PostPlacedNewNode();
			Ce->AllocateDefaultPins();
		}
		else if (UK2Node_Event* Ev = Cast<UK2Node_Event>(Node))
		{
			const TSharedPtr<FJsonObject>* EvObj = nullptr;
			if (!Op->TryGetObjectField(TEXT("event_override"), EvObj) || !EvObj || !(*EvObj).IsValid())
			{
				OutErrors.Add(TEXT(
					"K2Node_Event requires event_override { function_name, outer_class_path }. For user-defined custom events use K2Node_CustomEvent + event_name."));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			FString FnStr, OuterStr;
			(*EvObj)->TryGetStringField(TEXT("function_name"), FnStr);
			(*EvObj)->TryGetStringField(TEXT("outer_class_path"), OuterStr);
			if (OuterStr.IsEmpty() && !FnStr.IsEmpty())
			{
				UnrealAiBlueprintFunctionResolve::TryDefaultOuterClassPathForK2Event(FnStr, OuterStr);
			}
			UClass* OC = OuterStr.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *OuterStr);
			if (!OC || FnStr.IsEmpty())
			{
				OutErrors.Add(TEXT(
					"event_override requires function_name and outer_class_path (e.g. ReceiveBeginPlay + /Script/Engine.Actor; Jump or Landed + /Script/Engine.Character). Common lifecycle events default outer_class_path when omitted."));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			Ev->EventReference.SetExternalMember(FName(*FnStr), OC);
			Ev->bOverrideFunction = true;
			Ev->PostPlacedNewNode();
			Ev->AllocateDefaultPins();
		}
		else if (UK2Node_VariableGet* Vg = Cast<UK2Node_VariableGet>(Node))
		{
			FString VName;
			Op->TryGetStringField(TEXT("variable_name"), VName);
			if (VName.IsEmpty())
			{
				OutErrors.Add(TEXT("K2Node_VariableGet requires variable_name"));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			Vg->VariableReference.SetSelfMember(FName(*VName));
			Vg->PostPlacedNewNode();
			Vg->AllocateDefaultPins();
			Vg->ReconstructNode();
		}
		else if (UK2Node_VariableSet* Vs = Cast<UK2Node_VariableSet>(Node))
		{
			FString VName;
			Op->TryGetStringField(TEXT("variable_name"), VName);
			if (VName.IsEmpty())
			{
				OutErrors.Add(TEXT("K2Node_VariableSet requires variable_name"));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			Vs->VariableReference.SetSelfMember(FName(*VName));
			Vs->PostPlacedNewNode();
			Vs->AllocateDefaultPins();
			Vs->ReconstructNode();
		}
		else if (UK2Node_DynamicCast* Dc = Cast<UK2Node_DynamicCast>(Node))
		{
			FString TgtPath;
			Op->TryGetStringField(TEXT("cast_target_class"), TgtPath);
			if (TgtPath.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* CastObj = nullptr;
				if (Op->TryGetObjectField(TEXT("dynamic_cast"), CastObj) && CastObj && (*CastObj).IsValid())
				{
					(*CastObj)->TryGetStringField(TEXT("class_path"), TgtPath);
				}
			}
			UClass* Tgt = TgtPath.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *TgtPath);
			if (!Tgt)
			{
				OutErrors.Add(TEXT("K2Node_DynamicCast requires cast_target_class or dynamic_cast.class_path"));
				Graph->RemoveNode(Node);
				return nullptr;
			}
			Dc->TargetType = Tgt;
			Dc->PostPlacedNewNode();
			Dc->AllocateDefaultPins();
			Dc->ReconstructNode();
		}
		else
		{
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
			Node->ReconstructNode();
			(void)BP;
		}
		return Node;
	}

	/** Dry-run: structural checks for create_node without mutating the graph (see also semantic_kind path). */
	static FString ValidateCreateNodeForPatchDryRun(const TSharedPtr<FJsonObject>& Op, FString& OutPatchId)
	{
		OutPatchId.Reset();
		FString PatchId;
		if (!Op->TryGetStringField(TEXT("patch_id"), PatchId) || PatchId.IsEmpty())
		{
			return TEXT("create_node requires patch_id");
		}
		OutPatchId = PatchId;
		FString K2ClassPath;
		TArray<FString> Tmp;
		if (!TryResolveCreateNodeK2ClassPath(Op, K2ClassPath, Tmp))
		{
			return Tmp.Num() > 0 ? Tmp[0] : TEXT("create_node validation failed");
		}
		UClass* NodeClass = LoadObject<UClass>(nullptr, *K2ClassPath);
		if (!NodeClass || !NodeClass->IsChildOf(UK2Node::StaticClass()))
		{
			FString Hint;
			const FString Low = K2ClassPath.ToLower();
			if (Low.Contains(TEXT("intless")) || Low.Contains(TEXT("intadd")) || Low.Contains(TEXT("less_int"))
				|| Low.Contains(TEXT("add_int")) || Low.Contains(TEXT("k2node_math")))
			{
				Hint = TEXT(
					" Integer math/compare nodes are usually K2Node_CallFunction: class_path /Script/Engine.KismetMathLibrary, function_name Less_IntInt | Add_IntInt | Greater_IntInt | EqualEqual_IntInt.");
			}
			else if (Low.Contains(TEXT("literal")))
			{
				Hint = TEXT(
					" Literal ints: K2Node_CallFunction with KismetSystemLibrary.MakeLiteralInt (not KismetMathLibrary), or set_pin_default on an int input.");
			}
			return FString::Printf(TEXT("k2_class not a UK2Node: %s.%s"), *K2ClassPath, *Hint);
		}

		const TSharedPtr<FJsonObject>* CFObj = nullptr;
		if (Op->TryGetObjectField(TEXT("call_function"), CFObj) && CFObj && (*CFObj).IsValid())
		{
			if (!NodeClass->IsChildOf(UK2Node_CallFunction::StaticClass()))
			{
				return TEXT("call_function init requires K2Node_CallFunction k2_class");
			}
			FString ClsPath, FnName;
			(*CFObj)->TryGetStringField(TEXT("class_path"), ClsPath);
			(*CFObj)->TryGetStringField(TEXT("function_name"), FnName);
			UnrealAiBlueprintFunctionResolve::SplitCombinedClassPathAndFunctionName(ClsPath, FnName);
			UClass* Cls = ClsPath.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *ClsPath);
			if (!Cls || FnName.IsEmpty())
			{
				return TEXT("call_function requires class_path and function_name (split class::func if merged).");
			}
			if (!UnrealAiBlueprintFunctionResolve::ResolveCallFunction(Cls, FnName))
			{
				FString KismetHint;
				if (Cls == UKismetMathLibrary::StaticClass())
				{
					KismetHint = TEXT(" Integer equality on KismetMathLibrary is EqualEqual_IntInt (not Equal_IntInt).");
				}
				return FString::Printf(
					TEXT("Function not found on %s (after resolving name '%s'). Use class_path of the declaring UClass.%s"),
					*ClsPath,
					*FnName,
					*KismetHint);
			}
			return FString();
		}
		if (NodeClass->IsChildOf(UK2Node_CallFunction::StaticClass()))
		{
			FString ClsPath, FnName;
			Op->TryGetStringField(TEXT("class_path"), ClsPath);
			Op->TryGetStringField(TEXT("function_name"), FnName);
			ClsPath.TrimStartAndEndInline();
			FnName.TrimStartAndEndInline();
			UnrealAiBlueprintFunctionResolve::SplitCombinedClassPathAndFunctionName(ClsPath, FnName);
			if (ClsPath.IsEmpty() || FnName.IsEmpty())
			{
				return TEXT(
					"K2Node_CallFunction requires class_path + function_name on the op (top-level) or call_function { class_path, function_name }.");
			}
			UClass* Cls = LoadObject<UClass>(nullptr, *ClsPath);
			if (!Cls)
			{
				return FString::Printf(TEXT("class_path not found: %s"), *ClsPath);
			}
			if (!UnrealAiBlueprintFunctionResolve::ResolveCallFunction(Cls, FnName))
			{
				FString KismetHint;
				if (Cls == UKismetMathLibrary::StaticClass())
				{
					KismetHint = TEXT(" Integer equality on KismetMathLibrary is EqualEqual_IntInt (not Equal_IntInt).");
				}
				return FString::Printf(
					TEXT("Function not found on %s (after resolving name '%s'). See KismetMathLibrary for math; MakeLiteral* on KismetSystemLibrary.%s"),
					*ClsPath,
					*FnName,
					*KismetHint);
			}
			return FString();
		}
		if (NodeClass->IsChildOf(UK2Node_CustomEvent::StaticClass()))
		{
			FString EvName;
			Op->TryGetStringField(TEXT("event_name"), EvName);
			if (EvName.IsEmpty())
			{
				Op->TryGetStringField(TEXT("custom_event_name"), EvName);
			}
			const TSharedPtr<FJsonObject>* CEObj = nullptr;
			if (EvName.IsEmpty() && Op->TryGetObjectField(TEXT("custom_event"), CEObj) && CEObj && (*CEObj).IsValid())
			{
				(*CEObj)->TryGetStringField(TEXT("name"), EvName);
				if (EvName.IsEmpty())
				{
					(*CEObj)->TryGetStringField(TEXT("event_name"), EvName);
				}
			}
			if (EvName.IsEmpty())
			{
				Op->TryGetStringField(TEXT("name"), EvName);
			}
			if (EvName.IsEmpty())
			{
				return TEXT(
					"K2Node_CustomEvent needs event_name (or custom_event.name). User-defined events use k2_class /Script/BlueprintGraph.K2Node_CustomEvent — not K2Node_Event.");
			}
			return FString();
		}
		if (NodeClass->IsChildOf(UK2Node_Event::StaticClass()))
		{
			const TSharedPtr<FJsonObject>* EvObj = nullptr;
			if (!Op->TryGetObjectField(TEXT("event_override"), EvObj) || !EvObj || !(*EvObj).IsValid())
			{
				return TEXT(
					"K2Node_Event requires event_override { function_name, outer_class_path }. For user-defined custom events use K2Node_CustomEvent + event_name.");
			}
			FString FnStr, OuterStr;
			(*EvObj)->TryGetStringField(TEXT("function_name"), FnStr);
			(*EvObj)->TryGetStringField(TEXT("outer_class_path"), OuterStr);
			if (OuterStr.IsEmpty() && !FnStr.IsEmpty())
			{
				UnrealAiBlueprintFunctionResolve::TryDefaultOuterClassPathForK2Event(FnStr, OuterStr);
			}
			UClass* OC = OuterStr.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *OuterStr);
			if (!OC || FnStr.IsEmpty())
			{
				return TEXT(
					"event_override requires function_name and outer_class_path (e.g. ReceiveBeginPlay + /Script/Engine.Actor; Jump or Landed + /Script/Engine.Character). Common lifecycle events default outer_class_path when omitted.");
			}
			return FString();
		}
		if (NodeClass->IsChildOf(UK2Node_VariableGet::StaticClass()))
		{
			FString VName;
			Op->TryGetStringField(TEXT("variable_name"), VName);
			if (VName.IsEmpty())
			{
				return TEXT("K2Node_VariableGet requires variable_name");
			}
			return FString();
		}
		if (NodeClass->IsChildOf(UK2Node_VariableSet::StaticClass()))
		{
			FString VName;
			Op->TryGetStringField(TEXT("variable_name"), VName);
			if (VName.IsEmpty())
			{
				return TEXT("K2Node_VariableSet requires variable_name");
			}
			return FString();
		}
		if (NodeClass->IsChildOf(UK2Node_DynamicCast::StaticClass()))
		{
			FString TgtPath;
			Op->TryGetStringField(TEXT("cast_target_class"), TgtPath);
			if (TgtPath.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* CastObj = nullptr;
				if (Op->TryGetObjectField(TEXT("dynamic_cast"), CastObj) && CastObj && (*CastObj).IsValid())
				{
					(*CastObj)->TryGetStringField(TEXT("class_path"), TgtPath);
				}
			}
			UClass* Tgt = TgtPath.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *TgtPath);
			if (!Tgt)
			{
				return TEXT("K2Node_DynamicCast requires cast_target_class or dynamic_cast.class_path");
			}
			return FString();
		}
		return FString();
	}

	static void AppendPinsJson(UEdGraphNode* N, TArray<TSharedPtr<FJsonValue>>& OutArr)
	{
		if (!N)
		{
			return;
		}
		for (UEdGraphPin* P : N->Pins)
		{
			if (!P || P->bHidden)
			{
				continue;
			}
			TSharedPtr<FJsonObject> Po = MakeShared<FJsonObject>();
			Po->SetStringField(TEXT("name"), P->PinName.ToString());
			Po->SetStringField(TEXT("direction"), (P->Direction == EGPD_Input) ? TEXT("input") : TEXT("output"));
			Po->SetStringField(TEXT("category"), P->PinType.PinCategory.ToString());
			if (!P->PinType.PinSubCategory.IsNone())
			{
				Po->SetStringField(TEXT("subcategory"), P->PinType.PinSubCategory.ToString());
			}
			if (!P->DefaultValue.IsEmpty())
			{
				Po->SetStringField(TEXT("default_value"), P->DefaultValue);
			}
			OutArr.Add(MakeShareable(new FJsonValueObject(Po.ToSharedRef())));
		}
	}

	static void AppendConnectAvailablePinsJson(
		const TSharedPtr<FJsonObject>& Payload,
		UEdGraphNode* FromNode,
		UEdGraphNode* ToNode)
	{
		if (!Payload.IsValid())
		{
			return;
		}
		if (FromNode)
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			AppendPinsJson(FromNode, Arr);
			Payload->SetArrayField(TEXT("available_pins_from"), Arr);
		}
		if (ToNode)
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			AppendPinsJson(ToNode, Arr);
			Payload->SetArrayField(TEXT("available_pins_to"), Arr);
		}
	}

	static bool FindSoleVisibleExecPin(UEdGraphNode* Node, EEdGraphPinDirection WantDir, UEdGraphPin*& OutPin, FString& OutErr)
	{
		OutPin = nullptr;
		OutErr.Reset();
		if (!Node)
		{
			OutErr = TEXT("null node");
			return false;
		}
		TArray<UEdGraphPin*> Hits;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (!P || P->bHidden)
			{
				continue;
			}
			if (P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				continue;
			}
			if (P->Direction != WantDir)
			{
				continue;
			}
			Hits.Add(P);
		}
		if (Hits.Num() == 0)
		{
			OutErr = (WantDir == EGPD_Output)
				? TEXT("no visible exec output pin on node (event overrides have no exec input; use connect with explicit pins or call a function)")
				: TEXT("no visible exec input pin on node");
			return false;
		}
		if (Hits.Num() > 1)
		{
			OutErr = FString::Printf(TEXT("ambiguous exec pins (%d visible matches); use op \"connect\" with explicit pin names"), Hits.Num());
			return false;
		}
		OutPin = Hits[0];
		return true;
	}

	struct FValidateOnlySpawnedNodesCleanup
	{
		UEdGraph* Graph = nullptr;
		TArray<UK2Node*>& Spawned;

		explicit FValidateOnlySpawnedNodesCleanup(UEdGraph* InGraph, TArray<UK2Node*>& InSpawned)
			: Graph(InGraph)
			, Spawned(InSpawned)
		{
		}
		~FValidateOnlySpawnedNodesCleanup()
		{
			for (UK2Node* N : Spawned)
			{
				if (N && Graph)
				{
					Graph->RemoveNode(N);
				}
			}
			Spawned.Empty();
		}
	};

	/** Stable user-visible copy; keep in sync with InferBlueprintGraphPatchErrorCode and suggested_correct_call matching. */
	static const TCHAR* GBlueprintParentClassMismatchMsg =
		TEXT("These nodes require a Blueprint whose parent inherits the type that declares this function or event; this asset's parent does not.");

	static FString JsonClassPathForUClass(const UClass* Cls)
	{
		if (!Cls)
		{
			return FString();
		}
		return Cls->GetClassPathName().ToString();
	}

	static void AppendErrorsDetailArray(
		const TArray<FString>& Errors,
		int32 FailedOpIdx,
		const TSharedPtr<FJsonObject>& OptionalEva,
		const TSharedPtr<FJsonObject>& Payload)
	{
		if (!Payload.IsValid() || Errors.Num() == 0)
		{
			return;
		}
		TArray<TSharedPtr<FJsonValue>> DetailArr;
		for (int32 i = 0; i < Errors.Num(); ++i)
		{
			TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("code"), InferBlueprintGraphPatchErrorCode(Errors[i]));
			D->SetStringField(TEXT("message"), Errors[i]);
			D->SetNumberField(TEXT("op_index"), static_cast<double>(FailedOpIdx));
			if (i == 0 && OptionalEva.IsValid())
			{
				D->SetObjectField(TEXT("expected_vs_actual"), OptionalEva);
			}
			DetailArr.Add(MakeShareable(new FJsonValueObject(D.ToSharedRef())));
		}
		Payload->SetArrayField(TEXT("errors_detail"), DetailArr);
	}

	/**
	 * Reject ops that imply a self type the Blueprint does not inherit (e.g. Character::Jump on a PlayerController Blueprint).
	 * Returns true if the batch may proceed; false if OutFailure should be returned immediately.
	 */
	static bool RunBlueprintGraphPatchSemanticPreflight(
		UBlueprint* BP,
		const TArray<TSharedPtr<FJsonValue>>& OpsArr,
		const FString& BlueprintPath,
		const FString& InGraphName,
		FUnrealAiToolInvocationResult& OutFailure)
	{
		if (!BP || !BP->ParentClass)
		{
			return true;
		}
		UClass* const BpParent = BP->ParentClass;
		for (int32 OpIdx = 0; OpIdx < OpsArr.Num(); ++OpIdx)
		{
			const TSharedPtr<FJsonValue>& OpVal = OpsArr[OpIdx];
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObj) || !OpObj || !(*OpObj).IsValid())
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpObj;
			FString OpName;
			Op->TryGetStringField(TEXT("op"), OpName);
			OpName.TrimStartAndEndInline();
			if (!OpName.Equals(TEXT("create_node"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			TArray<FString> TmpErrs;
			FString K2Path;
			if (!TryResolveCreateNodeK2ClassPath(Op, K2Path, TmpErrs))
			{
				continue;
			}
			UClass* NodeClass = LoadObject<UClass>(nullptr, *K2Path);
			if (!NodeClass || !NodeClass->IsChildOf(UK2Node::StaticClass()))
			{
				continue;
			}
			if (NodeClass->IsChildOf(UK2Node_Event::StaticClass()))
			{
				const TSharedPtr<FJsonObject>* EvObj = nullptr;
				if (!Op->TryGetObjectField(TEXT("event_override"), EvObj) || !EvObj || !(*EvObj).IsValid())
				{
					continue;
				}
				FString FnStr, OuterStr;
				(*EvObj)->TryGetStringField(TEXT("function_name"), FnStr);
				(*EvObj)->TryGetStringField(TEXT("outer_class_path"), OuterStr);
				if (OuterStr.IsEmpty() && !FnStr.IsEmpty())
				{
					UnrealAiBlueprintFunctionResolve::TryDefaultOuterClassPathForK2Event(FnStr, OuterStr);
				}
				UClass* OC = OuterStr.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *OuterStr);
				if (!OC)
				{
					continue;
				}
				if (!BpParent->IsChildOf(OC))
				{
					TSharedPtr<FJsonObject> Eva = MakeShared<FJsonObject>();
					Eva->SetStringField(TEXT("required_minimum_class"), JsonClassPathForUClass(OC));
					Eva->SetStringField(TEXT("blueprint_parent_class"), JsonClassPathForUClass(BpParent));
					Eva->SetStringField(TEXT("graph_name"), InGraphName);
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetBoolField(TEXT("ok"), false);
					O->SetStringField(TEXT("status"), TEXT("patch_validate_errors"));
					O->SetNumberField(TEXT("failed_op_index"), OpIdx);
					const FString E = GBlueprintParentClassMismatchMsg;
					O->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(E))});
					O->SetArrayField(
						TEXT("error_codes"),
						TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(InferBlueprintGraphPatchErrorCode(E)))});
					AppendErrorsDetailArray(TArray<FString>{E}, OpIdx, Eva, O);
					O->SetObjectField(TEXT("expected_vs_actual"), Eva);
					O->SetStringField(
						TEXT("note"),
						TEXT("Semantic preflight failed before graph simulation. Target a Blueprint whose native parent inherits required_minimum_class, or use controller/pawn-appropriate APIs."));
					AppendFailedOpSnippetToPayload(&OpsArr, OpIdx, O);
					AppendBlueprintGraphPatchSuggestedCorrectCall(BlueprintPath, InGraphName, E, O);
					OutFailure.bOk = false;
					OutFailure.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
					OutFailure.ContentForModel = OutFailure.ErrorMessage;
					return false;
				}
				continue;
			}
			if (NodeClass->IsChildOf(UK2Node_CallFunction::StaticClass()))
			{
				FString ClsPath, FnName;
				const TSharedPtr<FJsonObject>* CFObj = nullptr;
				if (Op->TryGetObjectField(TEXT("call_function"), CFObj) && CFObj && (*CFObj).IsValid())
				{
					(*CFObj)->TryGetStringField(TEXT("class_path"), ClsPath);
					(*CFObj)->TryGetStringField(TEXT("function_name"), FnName);
				}
				else
				{
					Op->TryGetStringField(TEXT("class_path"), ClsPath);
					Op->TryGetStringField(TEXT("function_name"), FnName);
				}
				ClsPath.TrimStartAndEndInline();
				FnName.TrimStartAndEndInline();
				UnrealAiBlueprintFunctionResolve::SplitCombinedClassPathAndFunctionName(ClsPath, FnName);
				if (ClsPath.IsEmpty() || FnName.IsEmpty())
				{
					continue;
				}
				UClass* Cls = LoadObject<UClass>(nullptr, *ClsPath);
				if (!Cls)
				{
					continue;
				}
				FString FnMut(FnName);
				UFunction* Fn = UnrealAiBlueprintFunctionResolve::ResolveCallFunction(Cls, FnMut);
				if (!Fn)
				{
					continue;
				}
				if (Fn->HasAnyFunctionFlags(FUNC_Static))
				{
					continue;
				}
				if (!BpParent->IsChildOf(Cls))
				{
					TSharedPtr<FJsonObject> Eva = MakeShared<FJsonObject>();
					Eva->SetStringField(TEXT("required_minimum_class"), JsonClassPathForUClass(Cls));
					Eva->SetStringField(TEXT("blueprint_parent_class"), JsonClassPathForUClass(BpParent));
					Eva->SetStringField(TEXT("graph_name"), InGraphName);
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetBoolField(TEXT("ok"), false);
					O->SetStringField(TEXT("status"), TEXT("patch_validate_errors"));
					O->SetNumberField(TEXT("failed_op_index"), OpIdx);
					const FString E = GBlueprintParentClassMismatchMsg;
					O->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(E))});
					O->SetArrayField(
						TEXT("error_codes"),
						TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(InferBlueprintGraphPatchErrorCode(E)))});
					AppendErrorsDetailArray(TArray<FString>{E}, OpIdx, Eva, O);
					O->SetObjectField(TEXT("expected_vs_actual"), Eva);
					O->SetStringField(
						TEXT("note"),
						TEXT("Semantic preflight failed before graph simulation. Target a Blueprint whose native parent inherits required_minimum_class, or use controller/pawn-appropriate APIs."));
					AppendFailedOpSnippetToPayload(&OpsArr, OpIdx, O);
					AppendBlueprintGraphPatchSuggestedCorrectCall(BlueprintPath, InGraphName, E, O);
					OutFailure.bOk = false;
					OutFailure.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
					OutFailure.ContentForModel = OutFailure.ErrorMessage;
					return false;
				}
			}
		}
		return true;
	}

	static FString InferBlueprintGraphPatchErrorCode(const FString& Msg)
	{
		if (Msg.Contains(TEXT("These nodes require a Blueprint whose parent inherits")))
		{
			return TEXT("blueprint_parent_class_mismatch");
		}
		if (Msg.Contains(TEXT("patch ops count exceeds")) || Msg.Contains(TEXT("max_ops_per_patch")))
		{
			return TEXT("patch_batch_too_large");
		}
		if (Msg.Contains(TEXT("create_node requires k2_class")))
		{
			return TEXT("missing_k2_class");
		}
		if (Msg.Contains(TEXT("create_node requires patch_id")))
		{
			return TEXT("missing_patch_id");
		}
		if (Msg.Contains(TEXT("k2_class not a UK2Node")))
		{
			return TEXT("invalid_k2_class");
		}
		if (Msg.Contains(TEXT("T3D placeholder")))
		{
			return TEXT("t3d_placeholder_in_patch");
		}
		if (Msg.Contains(TEXT("Unknown node ref")))
		{
			return TEXT("unknown_node_ref");
		}
		if (Msg.Contains(TEXT("connect pin not found")))
		{
			return TEXT("pin_not_found");
		}
		if (Msg.Contains(TEXT("ambiguous exec pins")))
		{
			return TEXT("ambiguous_exec_pin");
		}
		if (Msg.Contains(TEXT("duplicate create_node patch_id")))
		{
			return TEXT("duplicate_patch_id");
		}
		if (Msg.Contains(TEXT("Could not connect")))
		{
			return TEXT("link_failed");
		}
		if (Msg.Contains(TEXT("connect requires from and to")))
		{
			return TEXT("connect_shape_invalid");
		}
		if (Msg.Contains(TEXT("add_variable requires name and type")))
		{
			return TEXT("add_variable_invalid");
		}
		if (Msg.Contains(TEXT("add_variable: unknown or unsupported type")))
		{
			return TEXT("add_variable_type");
		}
		if (Msg.Contains(TEXT("Invalid guid ref")))
		{
			return TEXT("invalid_guid_ref");
		}
		if (Msg.Contains(TEXT("Node guid not found")))
		{
			return TEXT("node_guid_not_found");
		}
		if (Msg.Contains(TEXT("Unknown semantic_kind")))
		{
			return TEXT("invalid_semantic_kind");
		}
		if (Msg.Contains(TEXT("Unknown op:")))
		{
			return TEXT("unknown_op");
		}
		if (Msg.Contains(TEXT("Each ops[] entry must be an object")))
		{
			return TEXT("op_not_object");
		}
		if (Msg.Contains(TEXT("ops[].op is required")))
		{
			return TEXT("missing_op_field");
		}
		if (Msg.Contains(TEXT("K2Node_Event requires")) || Msg.Contains(TEXT("event_override requires")))
		{
			return TEXT("event_override_shape");
		}
		if (Msg.Contains(TEXT("Function not found on")))
		{
			return TEXT("call_function_not_found");
		}
		return TEXT("patch_error");
	}

	static void AppendFailedOpSnippetToPayload(
		const TArray<TSharedPtr<FJsonValue>>* OpsArr,
		int32 FailedIdx,
		const TSharedPtr<FJsonObject>& Payload)
	{
		if (!Payload.IsValid() || FailedIdx == INDEX_NONE || !OpsArr || FailedIdx < 0 || FailedIdx >= OpsArr->Num())
		{
			return;
		}
		const TSharedPtr<FJsonValue>& V = (*OpsArr)[FailedIdx];
		if (!V.IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!V->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
		FString JsonStr;
		{
			const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
		}
		static constexpr int32 kMaxFailedOpChars = 4096;
		if (JsonStr.Len() <= kMaxFailedOpChars)
		{
			TSharedPtr<FJsonObject> Copy;
			const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonStr);
			if (FJsonSerializer::Deserialize(R, Copy) && Copy.IsValid())
			{
				Payload->SetObjectField(TEXT("failed_op"), Copy);
			}
		}
		else
		{
			Payload->SetStringField(
				TEXT("failed_op_json"),
				JsonStr.Left(kMaxFailedOpChars) + TEXT("...<truncated>"));
		}
	}

	static void AppendBlueprintGraphPatchSuggestedCorrectCall(
		const FString& BlueprintPath,
		const FString& GraphName,
		const FString& FirstError,
		const TSharedPtr<FJsonObject>& Payload)
	{
		if (!Payload.IsValid() || FirstError.IsEmpty())
		{
			return;
		}
		TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
		if (FirstError.Contains(TEXT("These nodes require a Blueprint whose parent inherits")))
		{
			Suggested->SetStringField(TEXT("tool_id"), TEXT("asset_index_fuzzy_search"));
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("query"), TEXT("character pawn"));
			A->SetStringField(TEXT("path_prefix"), TEXT("/Game"));
			A->SetStringField(TEXT("class_name_substring"), TEXT("Blueprint"));
			const TSharedPtr<FJsonObject>* Eva = nullptr;
			if (Payload->TryGetObjectField(TEXT("expected_vs_actual"), Eva) && Eva && (*Eva).IsValid())
			{
				FString Req;
				if ((*Eva)->TryGetStringField(TEXT("required_minimum_class"), Req) && !Req.IsEmpty())
				{
					A->SetStringField(TEXT("minimum_blueprint_parent_class_path"), Req);
				}
			}
			A->SetNumberField(TEXT("max_results"), 15.0);
			Suggested->SetObjectField(TEXT("arguments"), A);
			Suggested->SetStringField(
				TEXT("note"),
				TEXT("Use a Blueprint whose native parent inherits required_minimum_class (see expected_vs_actual). On a PlayerController (or other non-matching asset), implement on the possessed Pawn/Character Blueprint or use APIs valid for this asset's parent class."));
		}
		else if (FirstError.Contains(TEXT("connect pin not found")) || FirstError.Contains(TEXT("Could not connect"))
			|| FirstError.Contains(TEXT("connect_exec")) || FirstError.Contains(TEXT("set_pin_default: pin")))
		{
			Suggested->SetStringField(TEXT("tool_id"), TEXT("blueprint_graph_list_pins"));
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			A->SetStringField(TEXT("graph_name"), GraphName);
			A->SetStringField(
				TEXT("node_ref"),
				TEXT("Replace with node_guid from blueprint_graph_introspect (bare UUID or guid:UUID)."));
			Suggested->SetObjectField(TEXT("arguments"), A);
			Suggested->SetStringField(
				TEXT("note"),
				TEXT("Pin failures often include available_pins_from / available_pins_to on the parent error JSON — use those names before calling list_pins again."));
		}
		else if (FirstError.Contains(TEXT("Node guid not found")))
		{
			Suggested->SetStringField(TEXT("tool_id"), TEXT("blueprint_graph_introspect"));
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			A->SetStringField(TEXT("graph_name"), GraphName);
			Suggested->SetObjectField(TEXT("arguments"), A);
			Suggested->SetStringField(
				TEXT("note"),
				TEXT("Copy node_guid from introspect on the correct graph. If this payload includes guid_found_in_graphs, set graph_name to one of those and retry the patch. Never reuse node_guid from validate_only create_node (transient nodes are deleted)."));
		}
		else if (FirstError.Contains(TEXT("K2Node_Event requires")) || FirstError.Contains(TEXT("event_override requires")))
		{
			Suggested->SetStringField(TEXT("tool_id"), TEXT("blueprint_graph_patch"));
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			A->SetStringField(TEXT("graph_name"), GraphName);
			A->SetBoolField(TEXT("validate_only"), true);
			TArray<TSharedPtr<FJsonValue>> Ops;
			TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
			Op->SetStringField(TEXT("op"), TEXT("create_node"));
			Op->SetStringField(TEXT("patch_id"), TEXT("n_evt"));
			Op->SetStringField(TEXT("semantic_kind"), TEXT("event_override"));
			Op->SetNumberField(TEXT("x"), 0);
			Op->SetNumberField(TEXT("y"), 0);
			TSharedPtr<FJsonObject> Ev = MakeShared<FJsonObject>();
			Ev->SetStringField(TEXT("function_name"), TEXT("Landed"));
			Ev->SetStringField(TEXT("outer_class_path"), TEXT("/Script/Engine.Character"));
			Op->SetObjectField(TEXT("event_override"), Ev);
			Ops.Add(MakeShareable(new FJsonValueObject(Op.ToSharedRef())));
			A->SetArrayField(TEXT("ops"), Ops);
			Suggested->SetObjectField(TEXT("arguments"), A);
		}
		else if (FirstError.Contains(TEXT("Function not found on")))
		{
			Suggested->SetStringField(TEXT("tool_id"), TEXT("blueprint_graph_patch"));
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			A->SetStringField(TEXT("graph_name"), GraphName);
			A->SetBoolField(TEXT("validate_only"), true);
			TArray<TSharedPtr<FJsonValue>> Ops;
			const bool bLitInt = FirstError.Contains(TEXT("MakeLiteralInt"), ESearchCase::IgnoreCase);
			const bool bLitFloat = FirstError.Contains(TEXT("MakeLiteralFloat"), ESearchCase::IgnoreCase);
			const bool bLitBool = FirstError.Contains(TEXT("MakeLiteralBool"), ESearchCase::IgnoreCase);
			if (bLitInt || bLitFloat || bLitBool)
			{
				TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
				Op->SetStringField(TEXT("op"), TEXT("create_node"));
				Op->SetStringField(TEXT("patch_id"), TEXT("n_lit"));
				Op->SetStringField(TEXT("semantic_kind"), TEXT("call_library_function"));
				Op->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.KismetSystemLibrary"));
				Op->SetStringField(
					TEXT("function_name"),
					bLitFloat ? TEXT("MakeLiteralFloat") : (bLitBool ? TEXT("MakeLiteralBool") : TEXT("MakeLiteralInt")));
				Op->SetNumberField(TEXT("x"), 0);
				Op->SetNumberField(TEXT("y"), 0);
				Ops.Add(MakeShareable(new FJsonValueObject(Op.ToSharedRef())));
				TSharedPtr<FJsonObject> Sp = MakeShared<FJsonObject>();
				Sp->SetStringField(TEXT("op"), TEXT("set_pin_default"));
				Sp->SetStringField(TEXT("ref"), TEXT("n_lit.Value"));
				Sp->SetStringField(TEXT("value"), bLitFloat ? TEXT("1.0") : (bLitBool ? TEXT("true") : TEXT("1")));
				Ops.Add(MakeShareable(new FJsonValueObject(Sp.ToSharedRef())));
				A->SetArrayField(TEXT("ops"), Ops);
				Suggested->SetObjectField(TEXT("arguments"), A);
				Suggested->SetStringField(
					TEXT("note"),
					TEXT("Literals use KismetSystemLibrary MakeLiteral* on class_path /Script/Engine.KismetSystemLibrary; set_pin_default on patch_id.Value."));
			}
			else
			{
				TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
				Op->SetStringField(TEXT("op"), TEXT("create_node"));
				Op->SetStringField(TEXT("patch_id"), TEXT("n_jump_call"));
				Op->SetStringField(TEXT("k2_class"), TEXT("/Script/BlueprintGraph.K2Node_CallFunction"));
				Op->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.Character"));
				Op->SetStringField(TEXT("function_name"), TEXT("Jump"));
				Op->SetNumberField(TEXT("x"), 0);
				Op->SetNumberField(TEXT("y"), 0);
				Ops.Add(MakeShareable(new FJsonValueObject(Op.ToSharedRef())));
				A->SetArrayField(TEXT("ops"), Ops);
				Suggested->SetObjectField(TEXT("arguments"), A);
			}
		}
		else if (FirstError.Contains(TEXT("Unknown semantic_kind"), ESearchCase::IgnoreCase)
				 && (FirstError.Contains(TEXT("literal_int"), ESearchCase::IgnoreCase)
					 || FirstError.Contains(TEXT("literal_float"), ESearchCase::IgnoreCase)
					 || FirstError.Contains(TEXT("literal_bool"), ESearchCase::IgnoreCase)))
		{
			Suggested->SetStringField(TEXT("tool_id"), TEXT("blueprint_graph_patch"));
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			A->SetStringField(TEXT("graph_name"), GraphName);
			A->SetBoolField(TEXT("validate_only"), true);
			TArray<TSharedPtr<FJsonValue>> Ops;
			TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
			Op->SetStringField(TEXT("op"), TEXT("create_node"));
			Op->SetStringField(TEXT("patch_id"), TEXT("n_lit"));
			Op->SetStringField(TEXT("semantic_kind"), TEXT("call_library_function"));
			Op->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.KismetSystemLibrary"));
			Op->SetStringField(TEXT("function_name"), TEXT("MakeLiteralInt"));
			Op->SetNumberField(TEXT("x"), 0);
			Op->SetNumberField(TEXT("y"), 0);
			Ops.Add(MakeShareable(new FJsonValueObject(Op.ToSharedRef())));
			TSharedPtr<FJsonObject> Sp = MakeShared<FJsonObject>();
			Sp->SetStringField(TEXT("op"), TEXT("set_pin_default"));
			Sp->SetStringField(TEXT("ref"), TEXT("n_lit.Value"));
			Sp->SetStringField(TEXT("value"), TEXT("1"));
			Ops.Add(MakeShareable(new FJsonValueObject(Sp.ToSharedRef())));
			A->SetArrayField(TEXT("ops"), Ops);
			Suggested->SetObjectField(TEXT("arguments"), A);
			Suggested->SetStringField(
				TEXT("note"),
				TEXT("semantic_kind has no literal_int — use call_library_function + KismetSystemLibrary + MakeLiteralInt (or MakeLiteralFloat/Bool) and set_pin_default on patch_id.Value; the resolver also repairs literal_* aliases."));
		}
		else if ((FirstError.Contains(TEXT("Unknown semantic_kind"), ESearchCase::IgnoreCase)
					 && FirstError.Contains(TEXT("call_function"), ESearchCase::IgnoreCase))
				 || (FirstError.Contains(TEXT("Unknown op:"), ESearchCase::IgnoreCase)
					 && FirstError.Contains(TEXT("call_function"), ESearchCase::IgnoreCase)))
		{
			Suggested->SetStringField(TEXT("tool_id"), TEXT("blueprint_graph_patch"));
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			A->SetStringField(TEXT("graph_name"), GraphName);
			A->SetBoolField(TEXT("validate_only"), true);
			TArray<TSharedPtr<FJsonValue>> Ops;
			TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
			Op->SetStringField(TEXT("op"), TEXT("create_node"));
			Op->SetStringField(TEXT("patch_id"), TEXT("n_print_example"));
			Op->SetStringField(TEXT("semantic_kind"), TEXT("call_library_function"));
			Op->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.KismetSystemLibrary"));
			Op->SetStringField(TEXT("function_name"), TEXT("PrintString"));
			Op->SetNumberField(TEXT("x"), 0);
			Op->SetNumberField(TEXT("y"), 0);
			Ops.Add(MakeShareable(new FJsonValueObject(Op.ToSharedRef())));
			A->SetArrayField(TEXT("ops"), Ops);
			Suggested->SetObjectField(TEXT("arguments"), A);
			Suggested->SetStringField(
				TEXT("note"),
				TEXT("Use op create_node with semantic_kind call_library_function (not call_function) and class_path + function_name; resolver also repairs op:call_function and semantic_kind:call_function."));
		}
		else
		{
			Suggested->SetStringField(TEXT("tool_id"), TEXT("blueprint_graph_patch"));
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("blueprint_path"), BlueprintPath);
			A->SetStringField(TEXT("graph_name"), GraphName);
			TArray<TSharedPtr<FJsonValue>> Ops;
			if (FirstError.Contains(TEXT("create_node requires k2_class")) || FirstError.Contains(TEXT("k2_class not a UK2Node")))
			{
				const FString EL = FirstError.ToLower();
				const bool bMathish = EL.Contains(TEXT("integer math")) || EL.Contains(TEXT("kismetmathlibrary"))
					|| EL.Contains(TEXT("intless")) || EL.Contains(TEXT("intadd")) || EL.Contains(TEXT("less_int"))
					|| EL.Contains(TEXT("add_int")) || EL.Contains(TEXT("k2node_math")) || EL.Contains(TEXT("greater_int"))
					|| EL.Contains(TEXT("equalequal_int"));
				const bool bSequenceish = EL.Contains(TEXT("k2node_sequence")) || EL.Contains(TEXT("executionsequence"))
					|| (EL.Contains(TEXT("k2_class not a uk2node")) && EL.Contains(TEXT("sequence"))
						&& !EL.Contains(TEXT("k2node_executionsequence")));
				TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
				Op->SetStringField(TEXT("op"), TEXT("create_node"));
				Op->SetStringField(TEXT("patch_id"), TEXT("n_example"));
				Op->SetNumberField(TEXT("x"), 0);
				Op->SetNumberField(TEXT("y"), 0);
				if (bMathish)
				{
					Op->SetStringField(TEXT("k2_class"), TEXT("/Script/BlueprintGraph.K2Node_CallFunction"));
					Op->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.KismetMathLibrary"));
					Op->SetStringField(TEXT("function_name"), TEXT("Less_IntInt"));
				}
				else if (bSequenceish)
				{
					Op->SetStringField(TEXT("k2_class"), TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence"));
				}
				else
				{
					Op->SetStringField(TEXT("patch_id"), TEXT("n_branch"));
					Op->SetStringField(TEXT("k2_class"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"));
				}
				Ops.Add(MakeShareable(new FJsonValueObject(Op.ToSharedRef())));
			}
			else if (FirstError.Contains(TEXT("Unknown node ref")) || FirstError.Contains(TEXT("T3D placeholder")))
			{
				TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
				Op->SetStringField(TEXT("op"), TEXT("connect"));
				Op->SetStringField(TEXT("from"), TEXT("n_branch.Then"));
				Op->SetStringField(
					TEXT("to"),
					TEXT("guid:PASTE-NODE-GUID-FROM-blueprint_graph_introspect.Execute"));
				Ops.Add(MakeShareable(new FJsonValueObject(Op.ToSharedRef())));
			}
			else if (FirstError.Contains(TEXT("add_variable requires name and type")))
			{
				TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
				Op->SetStringField(TEXT("op"), TEXT("add_variable"));
				Op->SetStringField(TEXT("name"), TEXT("MyInt"));
				Op->SetStringField(TEXT("type"), TEXT("int"));
				Ops.Add(MakeShareable(new FJsonValueObject(Op.ToSharedRef())));
			}
			else
			{
				A->SetBoolField(TEXT("validate_only"), true);
				A->SetStringField(
					TEXT("hint"),
					TEXT("Fix ops[failed_op_index] from the error payload; prefer validate_only:true on the full batch before applying."));
				Ops.Reset();
			}
			A->SetArrayField(TEXT("ops"), Ops);
			Suggested->SetObjectField(TEXT("arguments"), A);
		}
		Payload->SetObjectField(TEXT("suggested_correct_call"), Suggested);
	}

	static constexpr int64 GMaxOpsJsonFileBytes = 96LL * 1024 * 1024;

	static bool OpsJsonRelativePathHasAllowedPrefix(FString Rel)
	{
		Rel.TrimStartAndEndInline();
		Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Rel.StartsWith(TEXT("/")))
		{
			Rel = Rel.Mid(1);
		}
		const FString L = Rel.ToLower();
		return L.StartsWith(TEXT("saved/")) || L.StartsWith(TEXT("harness_step/"));
	}

	static bool TryLoadOpsArrayFromProjectFile(const FString& RelPathIn, TArray<TSharedPtr<FJsonValue>>& OutOps, FString& OutErr)
	{
		FString Rel = RelPathIn;
		Rel.TrimStartAndEndInline();
		Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Rel.StartsWith(TEXT("/")))
		{
			Rel = Rel.Mid(1);
		}
		if (Rel.IsEmpty())
		{
			OutErr = TEXT("ops_json_path is empty");
			return false;
		}
		if (!OpsJsonRelativePathHasAllowedPrefix(Rel))
		{
			OutErr = TEXT("ops_json_path must be project-relative under Saved/ or harness_step/");
			return false;
		}
		FString Abs;
		FString PathErr;
		if (!UnrealAiResolveProjectFilePath(Rel, Abs, PathErr))
		{
			OutErr = PathErr;
			return false;
		}
		const int64 Sz = IFileManager::Get().FileSize(*Abs);
		if (Sz < 0)
		{
			OutErr = TEXT("ops_json_path: could not read file size");
			return false;
		}
		if (Sz > GMaxOpsJsonFileBytes)
		{
			OutErr = FString::Printf(
				TEXT("ops JSON file too large (%lld bytes; max %lld) — split the patch or raise limits in source"),
				Sz,
				GMaxOpsJsonFileBytes);
			return false;
		}
		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *Abs))
		{
			OutErr = TEXT("Failed to read ops JSON file");
			return false;
		}
		TSharedPtr<FJsonValue> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutErr = TEXT("Invalid JSON in ops file");
			return false;
		}
		if (Root->Type != EJson::Array)
		{
			OutErr = TEXT("ops JSON file must be a JSON array at the root");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Arr = Root->AsArray();
		if (Arr.Num() == 0)
		{
			OutErr = TEXT("ops JSON array must be non-empty");
			return false;
		}
		OutOps = Arr;
		return true;
	}

	FUnrealAiToolInvocationResult RunBlueprintGraphPatchValidateOnly(
		UBlueprint* BP,
		UEdGraph* Graph,
		const TArray<TSharedPtr<FJsonValue>>& OpsArr,
		const FString& BlueprintPath,
		const FString& InGraphName)
	{
		TSet<FString> VirtualPatchIds;
		TMap<FString, UEdGraphNode*> VirtualPatchNodes;
		TArray<UK2Node*> ValidateSpawnedK2Nodes;
		TSet<UEdGraphNode*> ValidateSpawnedSet;
		FValidateOnlySpawnedNodesCleanup ValidateSpawnCleanup(Graph, ValidateSpawnedK2Nodes);
		TArray<FString> Notes;
		const UEdGraphSchema_K2* SchemaK2 = GetDefault<UEdGraphSchema_K2>();
		for (int32 OpIdx = 0; OpIdx < OpsArr.Num(); ++OpIdx)
		{
			const TSharedPtr<FJsonValue>& OpVal = OpsArr[OpIdx];
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObj) || !OpObj || !(*OpObj).IsValid())
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetBoolField(TEXT("ok"), false);
				O->SetStringField(TEXT("status"), TEXT("patch_validate_errors"));
				O->SetNumberField(TEXT("failed_op_index"), OpIdx);
				const FString E = TEXT("Each ops[] entry must be an object");
				O->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(E))});
				O->SetArrayField(
					TEXT("error_codes"),
					TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(InferBlueprintGraphPatchErrorCode(E)))});
				AppendErrorsDetailArray(TArray<FString>{E}, OpIdx, nullptr, O);
				FString NoteBad = TEXT("validate_only: no graph changes were made. Fix the op at failed_op_index and retry.");
				if (OpIdx > 0)
				{
					O->SetNumberField(TEXT("valid_prefix_op_count"), static_cast<double>(OpIdx));
					NoteBad += TEXT(
						" Ops with index < failed_op_index passed this dry-run; you may apply that prefix alone in a follow-up blueprint_graph_patch call.");
				}
				O->SetStringField(TEXT("note"), NoteBad);
				AppendFailedOpSnippetToPayload(&OpsArr, OpIdx, O);
				AppendBlueprintGraphPatchSuggestedCorrectCall(BlueprintPath, InGraphName, E, O);
				FUnrealAiToolInvocationResult R;
				R.bOk = false;
				R.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
				R.ContentForModel = R.ErrorMessage;
				return R;
			}
			const TSharedPtr<FJsonObject>& Op = *OpObj;
			FString OpName;
			Op->TryGetStringField(TEXT("op"), OpName);
			OpName.TrimStartAndEndInline();
			if (OpName.IsEmpty())
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetBoolField(TEXT("ok"), false);
				O->SetStringField(TEXT("status"), TEXT("patch_validate_errors"));
				O->SetNumberField(TEXT("failed_op_index"), OpIdx);
				const FString E = TEXT("ops[].op is required");
				O->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(E))});
				O->SetArrayField(
					TEXT("error_codes"),
					TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(InferBlueprintGraphPatchErrorCode(E)))});
				AppendErrorsDetailArray(TArray<FString>{E}, OpIdx, nullptr, O);
				FString NoteEmpty = TEXT("validate_only: no graph changes were made. Fix the op at failed_op_index and retry.");
				if (OpIdx > 0)
				{
					O->SetNumberField(TEXT("valid_prefix_op_count"), static_cast<double>(OpIdx));
					NoteEmpty += TEXT(
						" Ops with index < failed_op_index passed this dry-run; you may apply that prefix alone in a follow-up blueprint_graph_patch call.");
				}
				O->SetStringField(TEXT("note"), NoteEmpty);
				AppendFailedOpSnippetToPayload(&OpsArr, OpIdx, O);
				AppendBlueprintGraphPatchSuggestedCorrectCall(BlueprintPath, InGraphName, E, O);
				FUnrealAiToolInvocationResult R;
				R.bOk = false;
				R.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
				R.ContentForModel = R.ErrorMessage;
				return R;
			}

			auto FailAt = [&](const FString& E, UEdGraphNode* PinFromNode = nullptr, UEdGraphNode* PinToNode = nullptr) -> FUnrealAiToolInvocationResult
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetBoolField(TEXT("ok"), false);
				O->SetStringField(TEXT("status"), TEXT("patch_validate_errors"));
				O->SetNumberField(TEXT("failed_op_index"), OpIdx);
				O->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(E))});
				O->SetArrayField(
					TEXT("error_codes"),
					TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(InferBlueprintGraphPatchErrorCode(E)))});
				AppendErrorsDetailArray(TArray<FString>{E}, OpIdx, nullptr, O);
				FString Note = TEXT(
					"validate_only: transient batch nodes were rolled back; the saved graph was not modified. Fix the op at failed_op_index and retry.");
				if (OpIdx > 0)
				{
					O->SetNumberField(TEXT("valid_prefix_op_count"), static_cast<double>(OpIdx));
					Note += TEXT(
						" Ops with index < failed_op_index passed this dry-run; you may apply that prefix alone in a follow-up blueprint_graph_patch call.");
				}
				O->SetStringField(TEXT("note"), Note);
				AppendConnectAvailablePinsJson(O, PinFromNode, PinToNode);
				AppendFailedOpSnippetToPayload(&OpsArr, OpIdx, O);
				AppendBlueprintGraphPatchSuggestedCorrectCall(BlueprintPath, InGraphName, E, O);
				FUnrealAiToolInvocationResult R;
				R.bOk = false;
				R.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
				R.ContentForModel = R.ErrorMessage;
				return R;
			};

			if (OpName.Equals(TEXT("create_node"), ESearchCase::IgnoreCase))
			{
				FString Pid;
				const FString DryErr = ValidateCreateNodeForPatchDryRun(Op, Pid);
				if (!DryErr.IsEmpty())
				{
					return FailAt(DryErr);
				}
				if (VirtualPatchNodes.Contains(Pid))
				{
					return FailAt(FString::Printf(TEXT("duplicate create_node patch_id \"%s\" in this ops[] batch"), *Pid));
				}
				TArray<FString> SpawnErrs;
				if (UK2Node* K2 = CreateNodeFromPatchOp(BP, Graph, Op, SpawnErrs))
				{
					VirtualPatchIds.Add(Pid);
					VirtualPatchNodes.Add(Pid, K2);
					ValidateSpawnedK2Nodes.Add(K2);
					ValidateSpawnedSet.Add(K2);
				}
				else
				{
					const FString E = SpawnErrs.Num() > 0 ? SpawnErrs[0] : TEXT("create_node failed during validate_only spawn");
					return FailAt(E);
				}
			}
			else if (OpName.Equals(TEXT("add_variable"), ESearchCase::IgnoreCase))
			{
				FString Vn, Tstr;
				Op->TryGetStringField(TEXT("name"), Vn);
				if (Vn.IsEmpty())
				{
					Op->TryGetStringField(TEXT("variable_name"), Vn);
				}
				if (Vn.IsEmpty())
				{
					Op->TryGetStringField(TEXT("member_name"), Vn);
				}
				if (!PatchExtractVariableTypeString(Op, Tstr))
				{
					Tstr.Reset();
				}
				if (Vn.IsEmpty() || Tstr.IsEmpty())
				{
					return FailAt(TEXT(
						"add_variable requires name and type — type may be a string (e.g. \"int\") or object { \"category\":\"int\", \"subcategory\":\"int32\" }"));
				}
				FEdGraphPinType Pt;
				if (!UnrealAiBlueprintTools_TryParsePinTypeFromString(Tstr, Pt))
				{
					return FailAt(FString::Printf(
						TEXT("add_variable: unknown or unsupported type \"%s\" (try int, float, bool, name, text, or /Script/... class path)"),
						*Tstr));
				}
				(void)BP;
			}
			else if (OpName.Equals(TEXT("connect"), ESearchCase::IgnoreCase))
			{
				FString FromS, ToS;
				Op->TryGetStringField(TEXT("from"), FromS);
				Op->TryGetStringField(TEXT("to"), ToS);
				if (FromS.IsEmpty())
				{
					Op->TryGetStringField(TEXT("link_from"), FromS);
				}
				if (ToS.IsEmpty())
				{
					Op->TryGetStringField(TEXT("link_to"), ToS);
				}
				FString NFrom, PFrom, NTo, PTo;
				if (!SplitNodePinRef(FromS, NFrom, PFrom) || !SplitNodePinRef(ToS, NTo, PTo))
				{
					return FailAt(TEXT("connect requires from and to as NodeRef.Pin (e.g. n1.Then -> n2.execute)"));
				}
				UEdGraphNode* NA = nullptr;
				UEdGraphNode* NB = nullptr;
				FString Err;
				if (!TryResolveNodePartValidateOnly(NFrom, VirtualPatchNodes, Graph, NA, Err, BP, nullptr))
				{
					return FailAt(FString::Printf(TEXT("connect from: %s"), *Err));
				}
				if (!TryResolveNodePartValidateOnly(NTo, VirtualPatchNodes, Graph, NB, Err, BP, nullptr))
				{
					return FailAt(FString::Printf(TEXT("connect to: %s"), *Err));
				}
				FString PErrA, PErrB;
				UEdGraphPin* PA = FindPin(NA, PFrom, &PErrA);
				UEdGraphPin* PB = FindPin(NB, PTo, &PErrB);
				if (!PA)
				{
					return FailAt(
						!PErrA.IsEmpty()
							? FString::Printf(TEXT("connect from pin %s: %s"), *FromS, *PErrA)
							: FString::Printf(TEXT("connect pin not found (%s)"), *FromS),
						NA,
						NB);
				}
				if (!PB)
				{
					return FailAt(
						!PErrB.IsEmpty()
							? FString::Printf(TEXT("connect to pin %s: %s"), *ToS, *PErrB)
							: FString::Printf(TEXT("connect pin not found (%s)"), *ToS),
						NA,
						NB);
				}
				if (PA->Direction != EGPD_Output || PB->Direction != EGPD_Input)
				{
					return FailAt(
						FString::Printf(
							TEXT("connect requires from=output and to=input; got %s -> %s (swap from/to or fix pin names)"),
							*PinDebugSummary(PA),
							*PinDebugSummary(PB)),
						NA,
						NB);
				}
				const bool bBothSpawned = ValidateSpawnedSet.Contains(NA) && ValidateSpawnedSet.Contains(NB);
				if (bBothSpawned)
				{
					FString LinkErr;
					if (!TryCreateDirectedLink(PA, PB, SchemaK2, &LinkErr))
					{
						return FailAt(FString::Printf(TEXT("connect %s -> %s: %s"), *FromS, *ToS, *LinkErr), NA, NB);
					}
				}
				else
				{
					Notes.Add(FString::Printf(
						TEXT("Op %d (connect): validated pin names/direction only; TryCreateConnection was not run because at least one endpoint is a saved graph node (avoids mutating user wires during validate_only)."),
						OpIdx));
				}
			}
			else if (OpName.Equals(TEXT("connect_exec"), ESearchCase::IgnoreCase))
			{
				FString FromRef, ToRef;
				Op->TryGetStringField(TEXT("from_node"), FromRef);
				if (FromRef.IsEmpty())
				{
					Op->TryGetStringField(TEXT("from"), FromRef);
				}
				Op->TryGetStringField(TEXT("to_node"), ToRef);
				if (ToRef.IsEmpty())
				{
					Op->TryGetStringField(TEXT("to"), ToRef);
				}
				FromRef.TrimStartAndEndInline();
				ToRef.TrimStartAndEndInline();
				if (FromRef.Contains(TEXT(".")) || ToRef.Contains(TEXT(".")))
				{
					return FailAt(TEXT("connect_exec: from_node/to_node must be a bare patch_id or guid (no .Pin); use op \"connect\" for explicit pins"));
				}
				if (FromRef.IsEmpty() || ToRef.IsEmpty())
				{
					return FailAt(TEXT("connect_exec requires from_node and to_node (or from / to) as node refs without pin suffix"));
				}
				UEdGraphNode* NA = nullptr;
				UEdGraphNode* NB = nullptr;
				FString Err;
				if (!TryResolveNodePartValidateOnly(FromRef, VirtualPatchNodes, Graph, NA, Err, BP, nullptr))
				{
					return FailAt(FString::Printf(TEXT("connect_exec from_node: %s"), *Err));
				}
				if (!TryResolveNodePartValidateOnly(ToRef, VirtualPatchNodes, Graph, NB, Err, BP, nullptr))
				{
					return FailAt(FString::Printf(TEXT("connect_exec to_node: %s"), *Err));
				}
				UEdGraphPin* PA = nullptr;
				UEdGraphPin* PB = nullptr;
				FString ExecErr;
				if (!FindSoleVisibleExecPin(NA, EGPD_Output, PA, ExecErr))
				{
					return FailAt(FString::Printf(TEXT("connect_exec from_node: %s"), *ExecErr), NA, NB);
				}
				if (!FindSoleVisibleExecPin(NB, EGPD_Input, PB, ExecErr))
				{
					return FailAt(FString::Printf(TEXT("connect_exec to_node: %s"), *ExecErr), NA, NB);
				}
				const bool bBothSpawned = ValidateSpawnedSet.Contains(NA) && ValidateSpawnedSet.Contains(NB);
				if (bBothSpawned)
				{
					FString LinkErr;
					if (!TryCreateDirectedLink(PA, PB, SchemaK2, &LinkErr))
					{
						return FailAt(FString::Printf(TEXT("connect_exec: %s"), *LinkErr), NA, NB);
					}
				}
				else
				{
					Notes.Add(FString::Printf(
						TEXT("Op %d (connect_exec): validated sole exec pins only; link simulation skipped (saved graph endpoint)."),
						OpIdx));
				}
			}
			else if (OpName.Equals(TEXT("break_link"), ESearchCase::IgnoreCase))
			{
				FString FromS, ToS;
				Op->TryGetStringField(TEXT("from"), FromS);
				Op->TryGetStringField(TEXT("to"), ToS);
				if (FromS.IsEmpty())
				{
					Op->TryGetStringField(TEXT("link_from"), FromS);
				}
				if (ToS.IsEmpty())
				{
					Op->TryGetStringField(TEXT("link_to"), ToS);
				}
				FString NFrom, PFrom, NTo, PTo;
				if (!SplitNodePinRef(FromS, NFrom, PFrom) || !SplitNodePinRef(ToS, NTo, PTo))
				{
					return FailAt(TEXT("break_link requires from and to as NodeRef.Pin"));
				}
				UEdGraphNode* NA = nullptr;
				UEdGraphNode* NB = nullptr;
				FString Err;
				if (!TryResolveNodePartValidateOnly(NFrom, VirtualPatchNodes, Graph, NA, Err, BP, nullptr))
				{
					return FailAt(FString::Printf(TEXT("break_link from: %s"), *Err));
				}
				if (!TryResolveNodePartValidateOnly(NTo, VirtualPatchNodes, Graph, NB, Err, BP, nullptr))
				{
					return FailAt(FString::Printf(TEXT("break_link to: %s"), *Err));
				}
				if (ValidateSpawnedSet.Contains(NA) || ValidateSpawnedSet.Contains(NB))
				{
					return FailAt(TEXT("break_link requires both ends to reference saved graph nodes (guid), not batch-local patch_id nodes"));
				}
				FString BrA, BrB;
				UEdGraphPin* PA = FindPin(NA, PFrom, &BrA);
				UEdGraphPin* PB = FindPin(NB, PTo, &BrB);
				if (!PA || !PB)
				{
					return FailAt(TEXT("break_link: pin not found"));
				}
				if (!PinsAreDirectlyLinked(PA, PB) && !PinsAreDirectlyLinked(PB, PA))
				{
					return FailAt(TEXT("break_link: no link between those pins"));
				}
			}
			else if (OpName.Equals(TEXT("set_pin_default"), ESearchCase::IgnoreCase))
			{
				FString Val;
				Op->TryGetStringField(TEXT("value"), Val);
				FString NPart, PName, Err;
				FString RefCombined;
				Op->TryGetStringField(TEXT("ref"), RefCombined);
				if (!RefCombined.IsEmpty() && SplitNodePinRef(RefCombined, NPart, PName))
				{
				}
				else
				{
					FString NodeRefCombined;
					Op->TryGetStringField(TEXT("node_ref"), NodeRefCombined);
					if (!NodeRefCombined.IsEmpty() && SplitNodePinRef(NodeRefCombined, NPart, PName))
					{
					}
					else
					{
						Op->TryGetStringField(TEXT("patch_id"), NPart);
						FString GuidOrCombined;
						Op->TryGetStringField(TEXT("node_guid"), GuidOrCombined);
						Op->TryGetStringField(TEXT("pin"), PName);
						if (PName.IsEmpty())
						{
							Op->TryGetStringField(TEXT("pin_name"), PName);
						}
						if (NPart.IsEmpty() && !GuidOrCombined.IsEmpty())
						{
							if (GuidOrCombined.Contains(TEXT(".")) && SplitNodePinRef(GuidOrCombined, NPart, PName))
							{
							}
							else if (GuidOrCombined.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
							{
								NPart = GuidOrCombined;
							}
							else
							{
								NPart = FString(TEXT("guid:")) + GuidOrCombined;
							}
						}
					}
				}
				if (NPart.IsEmpty() || PName.IsEmpty())
				{
					return FailAt(TEXT(
						"set_pin_default requires ref or node_ref as \"patchId.pin\" or \"guid:....pin\" OR patch_id+pin / node_guid+pin plus value"));
				}
				if (Val.IsEmpty())
				{
					return FailAt(TEXT("set_pin_default requires value"));
				}
				UEdGraphNode* N = nullptr;
				if (!TryResolveNodePartValidateOnly(NPart, VirtualPatchNodes, Graph, N, Err, BP, nullptr))
				{
					return FailAt(Err);
				}
				FString SpErr;
				if (!FindPin(N, PName, &SpErr))
				{
					return FailAt(
						!SpErr.IsEmpty()
							? FString::Printf(TEXT("set_pin_default: %s"), *SpErr)
							: FString::Printf(TEXT("set_pin_default: pin %s not on node"), *PName),
						N,
						nullptr);
				}
			}
			else if (OpName.Equals(TEXT("create_comment"), ESearchCase::IgnoreCase))
			{
				FString Pid;
				Op->TryGetStringField(TEXT("patch_id"), Pid);
				if (Pid.IsEmpty())
				{
					return FailAt(TEXT("create_comment requires patch_id"));
				}
				VirtualPatchIds.Add(Pid);
			}
			else if (OpName.Equals(TEXT("remove_node"), ESearchCase::IgnoreCase)
				|| OpName.Equals(TEXT("move_node"), ESearchCase::IgnoreCase))
			{
				FString Pid, GuidStr;
				Op->TryGetStringField(TEXT("patch_id"), Pid);
				Op->TryGetStringField(TEXT("node_guid"), GuidStr);
				UEdGraphNode* N = nullptr;
				FString E2;
				if (!Pid.IsEmpty())
				{
					if (VirtualPatchIds.Contains(Pid))
					{
						return FailAt(FString::Printf(
							TEXT("%s cannot target patch_id \"%s\" declared in this batch — use guid: for existing graph nodes."),
							*OpName,
							*Pid));
					}
					if (!TryResolveNodePartValidateOnly(Pid, VirtualPatchNodes, Graph, N, E2, BP, nullptr))
					{
						return FailAt(FString::Printf(TEXT("%s: %s"), *OpName, *E2));
					}
				}
				else if (!GuidStr.IsEmpty())
				{
					FString G = GuidStr.TrimStartAndEnd();
					if (!G.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
					{
						G = FString(TEXT("guid:")) + G;
					}
					if (!TryResolveNodePartValidateOnly(G, VirtualPatchNodes, Graph, N, E2, BP, nullptr))
					{
						return FailAt(FString::Printf(TEXT("%s: %s"), *OpName, *E2));
					}
				}
				else
				{
					return FailAt(FString::Printf(TEXT("%s requires patch_id or node_guid"), *OpName));
				}
				if (!N)
				{
					return FailAt(FString::Printf(TEXT("%s: could not resolve target node"), *OpName));
				}
			}
			else if (OpName.Equals(TEXT("splice_on_link"), ESearchCase::IgnoreCase))
			{
				FString FromS, ToS, InsPid;
				Op->TryGetStringField(TEXT("from"), FromS);
				Op->TryGetStringField(TEXT("to"), ToS);
				if (FromS.IsEmpty())
				{
					Op->TryGetStringField(TEXT("link_from"), FromS);
				}
				if (ToS.IsEmpty())
				{
					Op->TryGetStringField(TEXT("link_to"), ToS);
				}
				Op->TryGetStringField(TEXT("insert_patch_id"), InsPid);
				FString InPinStr, OutPinStr;
				Op->TryGetStringField(TEXT("insert_input_pin"), InPinStr);
				Op->TryGetStringField(TEXT("insert_output_pin"), OutPinStr);
				if (InPinStr.IsEmpty())
				{
					InPinStr = UEdGraphSchema_K2::PN_Execute.ToString();
				}
				if (OutPinStr.IsEmpty())
				{
					OutPinStr = UEdGraphSchema_K2::PN_Then.ToString();
				}
				FString NFrom, PFrom, NTo, PTo;
				if (!SplitNodePinRef(FromS, NFrom, PFrom) || !SplitNodePinRef(ToS, NTo, PTo) || InsPid.IsEmpty())
				{
					return FailAt(TEXT("splice_on_link requires from, to (NodeRef.Pin), and insert_patch_id"));
				}
				if (!VirtualPatchIds.Contains(InsPid))
				{
					return FailAt(TEXT("splice_on_link: insert_patch_id not found in this patch batch (declare create_node earlier)"));
				}
				UEdGraphNode* UpNode = nullptr;
				UEdGraphNode* DownNode = nullptr;
				FString SErr;
				if (!TryResolveNodePartValidateOnly(NFrom, VirtualPatchNodes, Graph, UpNode, SErr, BP, nullptr))
				{
					return FailAt(FString::Printf(TEXT("splice_on_link from: %s"), *SErr));
				}
				if (!TryResolveNodePartValidateOnly(NTo, VirtualPatchNodes, Graph, DownNode, SErr, BP, nullptr))
				{
					return FailAt(FString::Printf(TEXT("splice_on_link to: %s"), *SErr));
				}
				if (ValidateSpawnedSet.Contains(UpNode) || ValidateSpawnedSet.Contains(DownNode))
				{
					return FailAt(TEXT("splice_on_link requires from/to to reference saved graph nodes (guid), not batch-local patch_id nodes"));
				}
				FString SErrU, SErrD, SErrMi, SErrMo;
				UEdGraphPin* UpPin = FindPin(UpNode, PFrom, &SErrU);
				UEdGraphPin* DownPin = FindPin(DownNode, PTo, &SErrD);
				if (!UpPin || !DownPin)
				{
					return FailAt(TEXT("splice_on_link: pin resolution failed on graph nodes"));
				}
				(void)InPinStr;
				(void)OutPinStr;
				if (!PinsAreDirectlyLinked(UpPin, DownPin) && !PinsAreDirectlyLinked(DownPin, UpPin))
				{
					return FailAt(TEXT("splice_on_link: no direct link between from and to pins"));
				}
			}
			else
			{
				return FailAt(FString::Printf(TEXT("Unknown op: %s"), *OpName));
			}
		}

		TSharedPtr<FJsonObject> Ok = MakeShared<FJsonObject>();
		Ok->SetBoolField(TEXT("ok"), true);
		Ok->SetStringField(TEXT("status"), TEXT("patch_validated"));
		Ok->SetStringField(
			TEXT("note"),
			TEXT(
				"validate_only: batch-local create_node ops were spawned on-graph then removed; pins and links between those transient nodes were checked when both connect endpoints were batch-local. Saved-graph endpoints only get pin name/direction checks for connect (no TryCreateConnection). Compile may still fail after apply. Do not pass create_node node_guid from this dry-run to blueprint_graph_list_pins or a later patch—use patch_id only inside the same ops[], or run blueprint_graph_introspect after a real apply."));
		TArray<TSharedPtr<FJsonValue>> NoteVals;
		for (const FString& N : Notes)
		{
			NoteVals.Add(MakeShareable(new FJsonValueString(N)));
		}
		Ok->SetArrayField(TEXT("validation_notes"), NoteVals);
		TSharedPtr<FJsonObject> ArgsEcho = MakeShared<FJsonObject>();
		ArgsEcho->SetStringField(TEXT("blueprint_path"), BlueprintPath);
		ArgsEcho->SetStringField(TEXT("graph_name"), InGraphName);
		ArgsEcho->SetBoolField(TEXT("validate_only"), true);
		Ok->SetObjectField(TEXT("arguments_echo"), ArgsEcho);
		FUnrealAiToolInvocationResult R;
		R.bOk = true;
		R.ContentForModel = UnrealAiToolJson::SerializeObject(Ok);
		return R;
	}
} // namespace UnrealAiBlueprintGraphPatchPriv

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGraphPatch(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiBlueprintGraphPatchPriv;
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(Args, nullptr);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required"));
	}
	if (!UnrealAiBlueprintTools_IsGameWritableBlueprintPath(Path))
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path must be under /Game"));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	if (GraphName.IsEmpty())
	{
		GraphName = TEXT("EventGraph");
	}

	FString OpsJsonPath;
	Args->TryGetStringField(TEXT("ops_json_path"), OpsJsonPath);
	OpsJsonPath.TrimStartAndEndInline();

	const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
	Args->TryGetArrayField(TEXT("ops"), OpsArr);
	const int32 InlineOpsNum = OpsArr ? OpsArr->Num() : 0;

	if (!OpsJsonPath.IsEmpty())
	{
		if (InlineOpsNum > 0)
		{
			return UnrealAiToolJson::Error(TEXT("Pass either ops[] or ops_json_path, not both"));
		}
		TArray<TSharedPtr<FJsonValue>> Loaded;
		FString LoadErr;
		if (!TryLoadOpsArrayFromProjectFile(OpsJsonPath, Loaded, LoadErr))
		{
			return UnrealAiToolJson::Error(LoadErr);
		}
		Args->SetArrayField(TEXT("ops"), Loaded);
		UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(Args, nullptr);
		OpsArr = nullptr;
		Args->TryGetArrayField(TEXT("ops"), OpsArr);
	}

	if (!OpsArr || OpsArr->Num() == 0)
	{
		return UnrealAiToolJson::Error(
			TEXT("Provide non-empty ops[] or ops_json_path (UTF-8 JSON array of op objects under Saved/ or harness_step/)"));
	}

	const int32 MaxOpsAllowed = FMath::Max(8, GetDefault<UUnrealAiEditorSettings>()->MaxOpsPerBlueprintGraphPatch);
	if (OpsArr->Num() > MaxOpsAllowed)
	{
		const FString E = FString::Printf(
			TEXT("patch ops count exceeds configured max_ops_per_patch (%d); split the batch or use ops_json_path."),
			MaxOpsAllowed);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), false);
		O->SetStringField(TEXT("status"), TEXT("patch_validate_errors"));
		O->SetNumberField(TEXT("failed_op_index"), -1);
		O->SetNumberField(TEXT("max_ops"), static_cast<double>(MaxOpsAllowed));
		O->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(E))});
		O->SetArrayField(
			TEXT("error_codes"),
			TArray<TSharedPtr<FJsonValue>>{MakeShareable(new FJsonValueString(InferBlueprintGraphPatchErrorCode(E)))});
		AppendErrorsDetailArray(TArray<FString>{E}, -1, nullptr, O);
		O->SetStringField(
			TEXT("note"),
			TEXT("Reduce ops[] length below max_ops or raise MaxOpsPerBlueprintGraphPatch in Unreal AI Editor settings."));
		FUnrealAiToolInvocationResult R;
		R.bOk = false;
		R.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
		R.ContentForModel = R.ErrorMessage;
		return R;
	}

	FString LayoutScopeStr = TEXT("patched_nodes");
	Args->TryGetStringField(TEXT("layout_scope"), LayoutScopeStr);
	LayoutScopeStr.TrimStartAndEndInline();
	if (LayoutScopeStr.IsEmpty())
	{
		LayoutScopeStr = TEXT("patched_nodes");
	}
	if (!LayoutScopeStr.Equals(TEXT("patched_nodes"), ESearchCase::IgnoreCase)
		&& !LayoutScopeStr.Equals(TEXT("full_graph"), ESearchCase::IgnoreCase)
		&& !LayoutScopeStr.Equals(TEXT("patched_nodes_and_downstream_exec"), ESearchCase::IgnoreCase))
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("layout_scope must be patched_nodes, patched_nodes_and_downstream_exec, or full_graph (got '%s')"),
			*LayoutScopeStr));
	}
	FString LayoutAnchorStr = TEXT("neighbor");
	Args->TryGetStringField(TEXT("layout_anchor"), LayoutAnchorStr);
	LayoutAnchorStr.TrimStartAndEndInline();
	if (LayoutAnchorStr.IsEmpty())
	{
		LayoutAnchorStr = TEXT("neighbor");
	}
	if (!LayoutAnchorStr.Equals(TEXT("neighbor"), ESearchCase::IgnoreCase)
		&& !LayoutAnchorStr.Equals(TEXT("below_existing"), ESearchCase::IgnoreCase))
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("layout_anchor must be neighbor or below_existing (got '%s')"),
			*LayoutAnchorStr));
	}
	const bool bLayoutFullGraph = LayoutScopeStr.Equals(TEXT("full_graph"), ESearchCase::IgnoreCase);
	const bool bLayoutDownstreamExec = LayoutScopeStr.Equals(TEXT("patched_nodes_and_downstream_exec"), ESearchCase::IgnoreCase);
	const bool bLayoutAnchorBelowExisting = LayoutAnchorStr.Equals(TEXT("below_existing"), ESearchCase::IgnoreCase);

	FString BpLoadErr;
	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path, &BpLoadErr);
	if (!BP)
	{
		return UnrealAiToolJson::Error(BpLoadErr.IsEmpty()
			? TEXT("Could not load Blueprint (check path and registry entry; re-run asset discovery).")
			: BpLoadErr);
	}
	UEdGraph* Graph = UnrealAiBlueprintTools_FindGraphByName(BP, GraphName);
	if (!Graph)
	{
		return UnrealAiToolJson::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	FUnrealAiToolInvocationResult SemanticPreflightFailure;
	if (!RunBlueprintGraphPatchSemanticPreflight(BP, *OpsArr, Path, GraphName, SemanticPreflightFailure))
	{
		return SemanticPreflightFailure;
	}

	bool bValidateOnly = false;
	if (Args->HasField(TEXT("validate_only")))
	{
		Args->TryGetBoolField(TEXT("validate_only"), bValidateOnly);
	}
	if (bValidateOnly)
	{
		return RunBlueprintGraphPatchValidateOnly(BP, Graph, *OpsArr, Path, GraphName);
	}

	bool bSkipInternalValidate = false;
	Args->TryGetBoolField(TEXT("skip_internal_validate_before_apply"), bSkipInternalValidate);
	if (GetDefault<UUnrealAiEditorSettings>()->bBlueprintGraphPatchInternalValidateBeforeApply && !bSkipInternalValidate)
	{
		const FUnrealAiToolInvocationResult PreApplyValidate =
			RunBlueprintGraphPatchValidateOnly(BP, Graph, *OpsArr, Path, GraphName);
		if (!PreApplyValidate.bOk)
		{
			return PreApplyValidate;
		}
	}

	bool bCompile = true;
	Args->TryGetBoolField(TEXT("compile"), bCompile);

	bool bAutoLayout = true;
	if (Args->HasField(TEXT("auto_layout")))
	{
		Args->TryGetBoolField(TEXT("auto_layout"), bAutoLayout);
	}

	FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnBpGraphPatch", "Unreal AI: blueprint_graph_patch"));
	BP->Modify();
	Graph->Modify();

	TMap<FString, UEdGraphNode*> PatchMap;
	TArray<FString> Errors;
	TArray<TSharedPtr<FJsonValue>> Applied;
	TArray<UEdGraphNode*> LayoutNodes;
	TArray<FUnrealBlueprintIrNodeLayoutHint> LayoutHints;
	TArray<FCommentReflowJob> CommentReflowJobs;

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	bool bKeepPrefixOnFailure = false;
	TArray<FString> PrefixFailureErrors;
	int32 PrefixFailureOpIdx = INDEX_NONE;

	int32 FirstFailedOpIdx = INDEX_NONE;
	UEdGraphNode* PatchErrConnectFromNode = nullptr;
	UEdGraphNode* PatchErrConnectToNode = nullptr;
	TArray<FString> LastPatchErrGuidFoundInGraphs;
	for (int32 OpIdx = 0; OpIdx < OpsArr->Num(); ++OpIdx)
	{
		LastPatchErrGuidFoundInGraphs.Reset();
		PatchErrConnectFromNode = nullptr;
		PatchErrConnectToNode = nullptr;
		const TSharedPtr<FJsonValue>& OpVal = (*OpsArr)[OpIdx];
		const TSharedPtr<FJsonObject>* OpObj = nullptr;
		if (!OpVal->TryGetObject(OpObj) || !OpObj || !(*OpObj).IsValid())
		{
			Errors.Add(TEXT("Each ops[] entry must be an object"));
			FirstFailedOpIdx = OpIdx;
			break;
		}
		const TSharedPtr<FJsonObject>& Op = *OpObj;
		FString OpName;
		Op->TryGetStringField(TEXT("op"), OpName);
		OpName.TrimStartAndEndInline();
		if (OpName.IsEmpty())
		{
			Errors.Add(TEXT("ops[].op is required"));
			FirstFailedOpIdx = OpIdx;
			break;
		}
		if (OpName.Equals(TEXT("create_node"), ESearchCase::IgnoreCase))
		{
			if (UK2Node* K2 = CreateNodeFromPatchOp(BP, Graph, Op, Errors))
			{
				FString Pid;
				Op->TryGetStringField(TEXT("patch_id"), Pid);
				PatchMap.Add(Pid, K2);
				double LX = 0, LY = 0;
				Op->TryGetNumberField(TEXT("x"), LX);
				Op->TryGetNumberField(TEXT("y"), LY);
				FUnrealBlueprintIrNodeLayoutHint LH;
				LH.NodeId = Pid;
				LH.X = FMath::RoundToInt(LX);
				LH.Y = FMath::RoundToInt(LY);
				LayoutHints.Add(LH);
				LayoutNodes.Add(K2);
				TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
				Rec->SetStringField(TEXT("op"), TEXT("create_node"));
				Rec->SetStringField(TEXT("patch_id"), Pid);
				Rec->SetStringField(TEXT("node_guid"), LexToString(K2->NodeGuid));
				Applied.Add(MakeShareable(new FJsonValueObject(Rec.ToSharedRef())));
			}
		}
		else if (OpName.Equals(TEXT("add_variable"), ESearchCase::IgnoreCase))
		{
			FString Vn, Tstr;
			Op->TryGetStringField(TEXT("name"), Vn);
			if (Vn.IsEmpty())
			{
				Op->TryGetStringField(TEXT("variable_name"), Vn);
			}
			if (Vn.IsEmpty())
			{
				Op->TryGetStringField(TEXT("member_name"), Vn);
			}
			if (!PatchExtractVariableTypeString(Op, Tstr))
			{
				Tstr.Reset();
			}
			if (Vn.IsEmpty() || Tstr.IsEmpty())
			{
				Errors.Add(TEXT(
					"add_variable requires name and type — type may be a string (e.g. \"int\") or object { \"category\":\"int\", \"subcategory\":\"int32\" }"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			FEdGraphPinType Pt;
			if (!UnrealAiBlueprintTools_TryParsePinTypeFromString(Tstr, Pt))
			{
				Errors.Add(FString::Printf(
					TEXT("add_variable: unknown or unsupported type \"%s\" (try int, float, bool, name, text, or /Script/... class path)"),
					*Tstr));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			FBlueprintEditorUtils::AddMemberVariable(BP, FName(*Vn), Pt);
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("op"), TEXT("add_variable"));
			Rec->SetStringField(TEXT("name"), Vn);
			Applied.Add(MakeShareable(new FJsonValueObject(Rec.ToSharedRef())));
		}
		else if (OpName.Equals(TEXT("connect"), ESearchCase::IgnoreCase))
		{
			FString FromS, ToS;
			Op->TryGetStringField(TEXT("from"), FromS);
			Op->TryGetStringField(TEXT("to"), ToS);
			if (FromS.IsEmpty())
			{
				Op->TryGetStringField(TEXT("link_from"), FromS);
			}
			if (ToS.IsEmpty())
			{
				Op->TryGetStringField(TEXT("link_to"), ToS);
			}
			FString NFrom, PFrom, NTo, PTo, Err;
			if (!SplitNodePinRef(FromS, NFrom, PFrom) || !SplitNodePinRef(ToS, NTo, PTo))
			{
				Errors.Add(TEXT("connect requires from and to as NodeRef.Pin (e.g. n1.Then -> n2.execute)"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			UEdGraphNode* NA = ResolveNodePart(NFrom, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			if (!NA)
			{
				Errors.Add(FString::Printf(TEXT("connect from: %s"), *Err));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			UEdGraphNode* NB = ResolveNodePart(NTo, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			if (!NB)
			{
				Errors.Add(FString::Printf(TEXT("connect to: %s"), *Err));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			FString PErrA, PErrB;
			UEdGraphPin* PA = FindPin(NA, PFrom, &PErrA);
			UEdGraphPin* PB = FindPin(NB, PTo, &PErrB);
			if (!PA)
			{
				PatchErrConnectFromNode = NA;
				PatchErrConnectToNode = NB;
				Errors.Add(!PErrA.IsEmpty()
					? FString::Printf(TEXT("connect from pin %s: %s"), *FromS, *PErrA)
					: FString::Printf(TEXT("connect pin not found (%s)"), *FromS));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			if (!PB)
			{
				PatchErrConnectFromNode = NA;
				PatchErrConnectToNode = NB;
				Errors.Add(!PErrB.IsEmpty()
					? FString::Printf(TEXT("connect to pin %s: %s"), *ToS, *PErrB)
					: FString::Printf(TEXT("connect pin not found (%s)"), *ToS));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			FString LinkErr;
			if (!TryCreateDirectedLink(PA, PB, Schema, &LinkErr))
			{
				PatchErrConnectFromNode = NA;
				PatchErrConnectToNode = NB;
				Errors.Add(FString::Printf(TEXT("connect %s -> %s: %s"), *FromS, *ToS, *LinkErr));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("op"), TEXT("connect"));
			Rec->SetStringField(TEXT("from"), FromS);
			Rec->SetStringField(TEXT("to"), ToS);
			Applied.Add(MakeShareable(new FJsonValueObject(Rec.ToSharedRef())));
		}
		else if (OpName.Equals(TEXT("connect_exec"), ESearchCase::IgnoreCase))
		{
			FString FromRef, ToRef;
			Op->TryGetStringField(TEXT("from_node"), FromRef);
			if (FromRef.IsEmpty())
			{
				Op->TryGetStringField(TEXT("from"), FromRef);
			}
			Op->TryGetStringField(TEXT("to_node"), ToRef);
			if (ToRef.IsEmpty())
			{
				Op->TryGetStringField(TEXT("to"), ToRef);
			}
			FromRef.TrimStartAndEndInline();
			ToRef.TrimStartAndEndInline();
			if (FromRef.Contains(TEXT(".")) || ToRef.Contains(TEXT(".")))
			{
				Errors.Add(TEXT("connect_exec: from_node/to_node must be a bare patch_id or guid (no .Pin); use op \"connect\" for explicit pins"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			if (FromRef.IsEmpty() || ToRef.IsEmpty())
			{
				Errors.Add(TEXT("connect_exec requires from_node and to_node (or from / to) as node refs without pin suffix"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			FString Err;
			UEdGraphNode* NA = ResolveNodePart(FromRef, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			if (!NA)
			{
				Errors.Add(FString::Printf(TEXT("connect_exec from_node: %s"), *Err));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			UEdGraphNode* NB = ResolveNodePart(ToRef, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			if (!NB)
			{
				Errors.Add(FString::Printf(TEXT("connect_exec to_node: %s"), *Err));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			UEdGraphPin* PA = nullptr;
			UEdGraphPin* PB = nullptr;
			FString ExecErr;
			if (!FindSoleVisibleExecPin(NA, EGPD_Output, PA, ExecErr))
			{
				PatchErrConnectFromNode = NA;
				PatchErrConnectToNode = NB;
				Errors.Add(FString::Printf(TEXT("connect_exec from_node: %s"), *ExecErr));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			if (!FindSoleVisibleExecPin(NB, EGPD_Input, PB, ExecErr))
			{
				PatchErrConnectFromNode = NA;
				PatchErrConnectToNode = NB;
				Errors.Add(FString::Printf(TEXT("connect_exec to_node: %s"), *ExecErr));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			FString LinkErr;
			if (!TryCreateDirectedLink(PA, PB, Schema, &LinkErr))
			{
				PatchErrConnectFromNode = NA;
				PatchErrConnectToNode = NB;
				Errors.Add(FString::Printf(TEXT("connect_exec: %s"), *LinkErr));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("op"), TEXT("connect_exec"));
			Rec->SetStringField(TEXT("from_node"), FromRef);
			Rec->SetStringField(TEXT("to_node"), ToRef);
			Applied.Add(MakeShareable(new FJsonValueObject(Rec.ToSharedRef())));
		}
		else if (OpName.Equals(TEXT("set_pin_default"), ESearchCase::IgnoreCase))
		{
			FString Val;
			Op->TryGetStringField(TEXT("value"), Val);
			FString NPart, PName, Err;
			FString RefCombined;
			Op->TryGetStringField(TEXT("ref"), RefCombined);
			if (!RefCombined.IsEmpty() && SplitNodePinRef(RefCombined, NPart, PName))
			{
				// ok
			}
			else
			{
				FString NodeRefCombined;
				Op->TryGetStringField(TEXT("node_ref"), NodeRefCombined);
				if (!NodeRefCombined.IsEmpty() && SplitNodePinRef(NodeRefCombined, NPart, PName))
				{
					// ok — same as ref: guid.pin or patchId.pin
				}
				else
				{
				Op->TryGetStringField(TEXT("patch_id"), NPart);
				FString GuidOrCombined;
				Op->TryGetStringField(TEXT("node_guid"), GuidOrCombined);
				Op->TryGetStringField(TEXT("pin"), PName);
				if (PName.IsEmpty())
				{
					Op->TryGetStringField(TEXT("pin_name"), PName);
				}
				if (NPart.IsEmpty() && !GuidOrCombined.IsEmpty())
				{
					if (GuidOrCombined.Contains(TEXT(".")) && SplitNodePinRef(GuidOrCombined, NPart, PName))
					{
						// e.g. guid:xxxxxxxx.pin in node_guid field
					}
					else if (GuidOrCombined.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
					{
						NPart = GuidOrCombined;
					}
					else
					{
						NPart = FString(TEXT("guid:")) + GuidOrCombined;
					}
				}
				}
			}
			if (NPart.IsEmpty() || PName.IsEmpty())
			{
				Errors.Add(TEXT("set_pin_default requires ref or node_ref as \"patchId.pin\" or \"guid:....pin\" OR patch_id+pin / node_guid+pin plus value"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			if (Val.IsEmpty())
			{
				Errors.Add(TEXT("set_pin_default requires value"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			UEdGraphNode* N = ResolveNodePart(NPart, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			if (!N)
			{
				Errors.Add(Err);
				FirstFailedOpIdx = OpIdx;
				break;
			}
			FString SpErr;
			UEdGraphPin* P = FindPin(N, PName, &SpErr);
			if (!P)
			{
				Errors.Add(!SpErr.IsEmpty()
					? FString::Printf(TEXT("set_pin_default: %s"), *SpErr)
					: FString::Printf(TEXT("set_pin_default: pin %s not on node"), *PName));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			P->DefaultValue = Val;
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("op"), TEXT("set_pin_default"));
			Rec->SetStringField(TEXT("node"), NPart);
			Rec->SetStringField(TEXT("pin"), PName);
			Applied.Add(MakeShareable(new FJsonValueObject(Rec.ToSharedRef())));
		}
		else if (OpName.Equals(TEXT("break_link"), ESearchCase::IgnoreCase))
		{
			FString FromS, ToS;
			Op->TryGetStringField(TEXT("from"), FromS);
			Op->TryGetStringField(TEXT("to"), ToS);
			if (FromS.IsEmpty())
			{
				Op->TryGetStringField(TEXT("link_from"), FromS);
			}
			if (ToS.IsEmpty())
			{
				Op->TryGetStringField(TEXT("link_to"), ToS);
			}
			FString NFrom, PFrom, NTo, PTo, Err;
			if (!SplitNodePinRef(FromS, NFrom, PFrom) || !SplitNodePinRef(ToS, NTo, PTo))
			{
				Errors.Add(TEXT("break_link requires from and to as NodeRef.Pin"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			UEdGraphNode* NA = ResolveNodePart(NFrom, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			if (!NA)
			{
				Errors.Add(FString::Printf(TEXT("break_link from: %s"), *Err));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			UEdGraphNode* NB = ResolveNodePart(NTo, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			if (!NB)
			{
				Errors.Add(FString::Printf(TEXT("break_link to: %s"), *Err));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			FString BrA, BrB;
			UEdGraphPin* PA = FindPin(NA, PFrom, &BrA);
			UEdGraphPin* PB = FindPin(NB, PTo, &BrB);
			if (!PA || !PB)
			{
				if (!PA && !BrA.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("break_link from pin: %s"), *BrA));
				}
				else if (!PB && !BrB.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("break_link to pin: %s"), *BrB));
				}
				else
				{
					Errors.Add(TEXT("break_link: pin not found"));
				}
				FirstFailedOpIdx = OpIdx;
				break;
			}
			if (!TryBreakPinLink(PA, PB, Schema))
			{
				Errors.Add(TEXT("break_link: no link between those pins"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("op"), TEXT("break_link"));
			Rec->SetStringField(TEXT("from"), FromS);
			Rec->SetStringField(TEXT("to"), ToS);
			Applied.Add(MakeShareable(new FJsonValueObject(Rec.ToSharedRef())));
		}
		else if (OpName.Equals(TEXT("splice_on_link"), ESearchCase::IgnoreCase))
		{
			FString FromS, ToS, InsPid, InPinStr, OutPinStr;
			Op->TryGetStringField(TEXT("from"), FromS);
			Op->TryGetStringField(TEXT("to"), ToS);
			if (FromS.IsEmpty())
			{
				Op->TryGetStringField(TEXT("link_from"), FromS);
			}
			if (ToS.IsEmpty())
			{
				Op->TryGetStringField(TEXT("link_to"), ToS);
			}
			Op->TryGetStringField(TEXT("insert_patch_id"), InsPid);
			Op->TryGetStringField(TEXT("insert_input_pin"), InPinStr);
			Op->TryGetStringField(TEXT("insert_output_pin"), OutPinStr);
			if (InPinStr.IsEmpty())
			{
				InPinStr = UEdGraphSchema_K2::PN_Execute.ToString();
			}
			if (OutPinStr.IsEmpty())
			{
				OutPinStr = UEdGraphSchema_K2::PN_Then.ToString();
			}
			FString NFrom, PFrom, NTo, PTo, Err;
			if (!SplitNodePinRef(FromS, NFrom, PFrom) || !SplitNodePinRef(ToS, NTo, PTo) || InsPid.IsEmpty())
			{
				Errors.Add(TEXT("splice_on_link requires from, to (NodeRef.Pin), and insert_patch_id"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			UEdGraphNode* UpNode = ResolveNodePart(NFrom, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			if (!UpNode)
			{
				Errors.Add(FString::Printf(TEXT("splice_on_link from: %s"), *Err));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			UEdGraphNode* DownNode = ResolveNodePart(NTo, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			if (!DownNode)
			{
				Errors.Add(FString::Printf(TEXT("splice_on_link to: %s"), *Err));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			UEdGraphNode* const* MidPtr = PatchMap.Find(InsPid);
			if (!MidPtr || !*MidPtr)
			{
				Errors.Add(TEXT("splice_on_link: insert_patch_id not found in this patch batch"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			FString SErrU, SErrD, SErrMi, SErrMo;
			UEdGraphPin* UpPin = FindPin(UpNode, PFrom, &SErrU);
			UEdGraphPin* DownPin = FindPin(DownNode, PTo, &SErrD);
			UEdGraphPin* MidIn = FindPin(*MidPtr, InPinStr, &SErrMi);
			UEdGraphPin* MidOut = FindPin(*MidPtr, OutPinStr, &SErrMo);
			if (!UpPin || !DownPin || !MidIn || !MidOut)
			{
				if (!UpPin && !SErrU.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("splice_on_link from pin: %s"), *SErrU));
				}
				else if (!DownPin && !SErrD.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("splice_on_link to pin: %s"), *SErrD));
				}
				else if (!MidIn && !SErrMi.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("splice_on_link insert_input_pin: %s"), *SErrMi));
				}
				else if (!MidOut && !SErrMo.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("splice_on_link insert_output_pin: %s"), *SErrMo));
				}
				else
				{
					Errors.Add(TEXT("splice_on_link: pin resolution failed (check insert_input_pin / insert_output_pin)"));
				}
				FirstFailedOpIdx = OpIdx;
				break;
			}
			if (!TryBreakPinLink(UpPin, DownPin, Schema))
			{
				Errors.Add(TEXT("splice_on_link: no direct link between from and to pins"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			SeedInsertedNodeBetweenNeighbors(*MidPtr, UpNode, DownNode);
			for (int32 Li = 0; Li < LayoutNodes.Num() && Li < LayoutHints.Num(); ++Li)
			{
				if (LayoutNodes[Li] == *MidPtr)
				{
					LayoutHints[Li].X = (*MidPtr)->NodePosX;
					LayoutHints[Li].Y = (*MidPtr)->NodePosY;
					break;
				}
			}
			FString ReErr;
			if (!TryCreateDirectedLink(UpPin, MidIn, Schema, &ReErr))
			{
				Errors.Add(FString::Printf(TEXT("splice_on_link reconnect: %s"), *ReErr));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			if (!TryCreateDirectedLink(MidOut, DownPin, Schema, &ReErr))
			{
				Errors.Add(FString::Printf(TEXT("splice_on_link reconnect: %s"), *ReErr));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("op"), TEXT("splice_on_link"));
			Rec->SetStringField(TEXT("insert_patch_id"), InsPid);
			Applied.Add(MakeShareable(new FJsonValueObject(Rec.ToSharedRef())));
		}
		else if (OpName.Equals(TEXT("create_comment"), ESearchCase::IgnoreCase))
		{
			FString Pid;
			Op->TryGetStringField(TEXT("patch_id"), Pid);
			if (Pid.IsEmpty())
			{
				Errors.Add(TEXT("create_comment requires patch_id"));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			double CXD = 0, CYD = 0;
			Op->TryGetNumberField(TEXT("x"), CXD);
			Op->TryGetNumberField(TEXT("y"), CYD);
			int32 W = 400;
			int32 H = 200;
			double WD = 0, HD = 0;
			if (Op->TryGetNumberField(TEXT("width"), WD))
			{
				W = FMath::Max(32, FMath::RoundToInt(WD));
			}
			if (Op->TryGetNumberField(TEXT("height"), HD))
			{
				H = FMath::Max(32, FMath::RoundToInt(HD));
			}
			FString Title = TEXT("Comment");
			Op->TryGetStringField(TEXT("title"), Title);
			if (Title.IsEmpty())
			{
				Op->TryGetStringField(TEXT("name"), Title);
			}
			if (Title.IsEmpty())
			{
				Title = TEXT("Comment");
			}
			UEdGraphNode_Comment* C = NewObject<UEdGraphNode_Comment>(Graph);
			Graph->AddNode(C, true, false);
			C->NodePosX = FMath::RoundToInt(CXD);
			C->NodePosY = FMath::RoundToInt(CYD);
			C->CreateNewGuid();
			C->NodeComment = Title;
			C->NodeWidth = W;
			C->NodeHeight = H;
			C->PostPlacedNewNode();
			PatchMap.Add(Pid, C);
			FUnrealBlueprintIrNodeLayoutHint LH;
			LH.NodeId = Pid;
			LH.X = C->NodePosX;
			LH.Y = C->NodePosY;
			LayoutHints.Add(LH);
			LayoutNodes.Add(C);
			FCommentReflowJob Job;
			Job.Comment = C;
			const TArray<TSharedPtr<FJsonValue>>* MArr = nullptr;
			if (Op->TryGetArrayField(TEXT("member_node_refs"), MArr) && MArr)
			{
				for (const TSharedPtr<FJsonValue>& Mv : *MArr)
				{
					FString Ms;
					if (Mv.IsValid() && Mv->TryGetString(Ms))
					{
						Ms.TrimStartAndEndInline();
						if (!Ms.IsEmpty())
						{
							Job.MemberNodeParts.Add(Ms);
						}
					}
				}
			}
			CommentReflowJobs.Add(MoveTemp(Job));
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("op"), TEXT("create_comment"));
			Rec->SetStringField(TEXT("patch_id"), Pid);
			Rec->SetStringField(TEXT("node_guid"), LexToString(C->NodeGuid));
			Applied.Add(MakeShareable(new FJsonValueObject(Rec.ToSharedRef())));
		}
		else if (OpName.Equals(TEXT("remove_node"), ESearchCase::IgnoreCase))
		{
			FString Pid, GuidStr;
			Op->TryGetStringField(TEXT("patch_id"), Pid);
			Op->TryGetStringField(TEXT("node_guid"), GuidStr);
			UEdGraphNode* N = nullptr;
			FString Err;
			if (!Pid.IsEmpty())
			{
				N = ResolveNodePart(Pid, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			}
			else if (!GuidStr.IsEmpty())
			{
				FString G = GuidStr.TrimStartAndEnd();
				if (!G.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
				{
					G = FString(TEXT("guid:")) + G;
				}
				N = ResolveNodePart(G, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			}
			if (!N)
			{
				Errors.Add(FString::Printf(TEXT("remove_node: %s"), *Err));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			FBlueprintEditorUtils::RemoveNode(BP, N, true);
			if (!Pid.IsEmpty())
			{
				PatchMap.Remove(Pid);
			}
			for (int32 Li = LayoutNodes.Num() - 1; Li >= 0; --Li)
			{
				if (LayoutNodes[Li] == N)
				{
					LayoutNodes.RemoveAt(Li);
					if (LayoutHints.IsValidIndex(Li))
					{
						LayoutHints.RemoveAt(Li);
					}
					break;
				}
			}
			for (int32 Ci = CommentReflowJobs.Num() - 1; Ci >= 0; --Ci)
			{
				if (CommentReflowJobs[Ci].Comment == N)
				{
					CommentReflowJobs.RemoveAt(Ci);
				}
			}
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("op"), TEXT("remove_node"));
			Applied.Add(MakeShareable(new FJsonValueObject(Rec.ToSharedRef())));
		}
		else if (OpName.Equals(TEXT("move_node"), ESearchCase::IgnoreCase))
		{
			FString Pid, Err;
			Op->TryGetStringField(TEXT("patch_id"), Pid);
			FString GuidS;
			Op->TryGetStringField(TEXT("node_guid"), GuidS);
			UEdGraphNode* N = nullptr;
			if (!Pid.IsEmpty())
			{
				N = ResolveNodePart(Pid, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			}
			else if (!GuidS.IsEmpty())
			{
				FString G = GuidS.TrimStartAndEnd();
				if (!G.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
				{
					G = FString(TEXT("guid:")) + G;
				}
				N = ResolveNodePart(G, PatchMap, Graph, Err, BP, &LastPatchErrGuidFoundInGraphs);
			}
			if (!N)
			{
				Errors.Add(FString::Printf(TEXT("move_node: %s"), *Err));
				FirstFailedOpIdx = OpIdx;
				break;
			}
			double XD = 0, YD = 0;
			Op->TryGetNumberField(TEXT("x"), XD);
			Op->TryGetNumberField(TEXT("y"), YD);
			N->NodePosX = FMath::RoundToInt(XD);
			N->NodePosY = FMath::RoundToInt(YD);
			N->Modify();
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("op"), TEXT("move_node"));
			Applied.Add(MakeShareable(new FJsonValueObject(Rec.ToSharedRef())));
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("Unknown op: %s"), *OpName));
		}
		if (Errors.Num() > 0)
		{
			FirstFailedOpIdx = OpIdx;
			break;
		}
	}

	if (Errors.Num() > 0)
	{
		const bool bAllowKeepPrefix = GetDefault<UUnrealAiEditorSettings>()->bBlueprintGraphPatchKeepOpsOnFailure
			&& Applied.Num() > 0;
		if (bAllowKeepPrefix)
		{
			bKeepPrefixOnFailure = true;
			PrefixFailureErrors = MoveTemp(Errors);
			PrefixFailureOpIdx = FirstFailedOpIdx;
			Errors.Reset();
		}
		else
		{
			Txn.Cancel();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("ok"), false);
			O->SetStringField(TEXT("status"), TEXT("patch_errors"));
			O->SetStringField(
				TEXT("note"),
				TEXT("Patch was not applied (transaction cancelled). applied_partial is always empty on failure."));
			if (FirstFailedOpIdx != INDEX_NONE)
			{
				O->SetNumberField(TEXT("failed_op_index"), FirstFailedOpIdx);
				AppendFailedOpSnippetToPayload(OpsArr, FirstFailedOpIdx, O);
			}
			TArray<TSharedPtr<FJsonValue>> EArr;
			TArray<TSharedPtr<FJsonValue>> CodeArr;
			for (const FString& E : Errors)
			{
				EArr.Add(MakeShareable(new FJsonValueString(E)));
				CodeArr.Add(MakeShareable(
					new FJsonValueString(InferBlueprintGraphPatchErrorCode(E))));
			}
			O->SetArrayField(TEXT("errors"), EArr);
			O->SetArrayField(TEXT("error_codes"), CodeArr);
			AppendErrorsDetailArray(Errors, FirstFailedOpIdx != INDEX_NONE ? FirstFailedOpIdx : -1, nullptr, O);
			O->SetArrayField(TEXT("applied_partial"), TArray<TSharedPtr<FJsonValue>>());
			if (LastPatchErrGuidFoundInGraphs.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Gf;
				for (const FString& S : LastPatchErrGuidFoundInGraphs)
				{
					Gf.Add(MakeShareable(new FJsonValueString(S)));
				}
				O->SetArrayField(TEXT("guid_found_in_graphs"), Gf);
			}
			AppendConnectAvailablePinsJson(O, PatchErrConnectFromNode, PatchErrConnectToNode);
			AppendBlueprintGraphPatchSuggestedCorrectCall(Path, GraphName, Errors[0], O);
			FUnrealAiToolInvocationResult R;
			R.bOk = false;
			R.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
			R.ContentForModel = R.ErrorMessage;
			return R;
		}
	}

	UnrealAiBlueprintFormatterBridge::EnsureFormatterModuleLoaded(nullptr);
	const FUnrealBlueprintGraphFormatOptions FmtOpts =
		UnrealAiBlueprintTools_MakeFormatOptionsFromSettings(GetDefault<UUnrealAiEditorSettings>());
	FUnrealBlueprintGraphFormatResult LayoutResult;
	bool bLayoutHintsIgnored = false;
	bool bLayoutDownstreamTruncated = false;
	int32 LayoutExtraDownstreamNodes = 0;
	bool bLayoutLocalSkipped = false;

	TArray<UEdGraphNode*> LayoutNodesForLayout = LayoutNodes;
	TArray<FUnrealBlueprintIrNodeLayoutHint> HintsForLayout = LayoutHints;
	if (bAutoLayout && !bLayoutFullGraph && bLayoutDownstreamExec && LayoutNodesForLayout.Num() > 0)
	{
		ExpandLayoutNodesWithDownstreamExec(Graph, LayoutNodesForLayout, bLayoutDownstreamTruncated, LayoutExtraDownstreamNodes);
		HintsForLayout.Reset();
		bLayoutHintsIgnored = true;
	}

	if (bAutoLayout)
	{
		if (bLayoutFullGraph)
		{
			LayoutResult = UnrealAiBlueprintFormatterBridge::TryLayoutEntireGraph(Graph, true, FmtOpts);
		}
		else if (LayoutNodesForLayout.Num() > 0)
		{
			const int32 ScriptLayoutCount = CountScriptLayoutNodesIn(LayoutNodesForLayout);
			if (ScriptLayoutCount <= 1)
			{
				bLayoutLocalSkipped = true;
			}
			else
			{
				if (bLayoutAnchorBelowExisting)
				{
					ApplyBelowExistingVerticalAnchor(Graph, LayoutNodesForLayout, LayoutBelowExistingMarginY);
				}
				LayoutResult = UnrealAiBlueprintFormatterBridge::TryLayoutAfterAiIrApply(
					Graph,
					LayoutNodesForLayout,
					HintsForLayout,
					true,
					FmtOpts);
			}
		}
	}

	if (bLayoutDownstreamTruncated)
	{
		LayoutResult.Warnings.Add(FString::Printf(
			TEXT("layout_downstream_truncated:true (max_nodes=%d)"),
			LayoutDownstreamMaxTotalNodes));
	}

	for (FCommentReflowJob& Job : CommentReflowJobs)
	{
		if (!Job.Comment)
		{
			continue;
		}
		TArray<UEdGraphNode*> Mem;
		for (const FString& Part : Job.MemberNodeParts)
		{
			FString E2;
			if (UEdGraphNode* N = ResolveNodePart(Part, PatchMap, Graph, E2, nullptr, nullptr))
			{
				if (!Cast<UEdGraphNode_Comment>(N))
				{
					Mem.Add(N);
				}
			}
		}
		if (Mem.Num() > 0)
		{
			UnrealBlueprintCommentReflow::FitCommentAroundNodes(Job.Comment, Mem, 80);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	if (bCompile)
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), BP->Status != BS_Error);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("graph_name"), GraphName);
	O->SetArrayField(TEXT("applied"), Applied);
	O->SetNumberField(TEXT("blueprint_status"), static_cast<double>(static_cast<int32>(BP->Status)));
	O->SetBoolField(TEXT("compiled"), bCompile);
	O->SetBoolField(TEXT("auto_layout"), bAutoLayout);
	O->SetStringField(TEXT("layout_scope"), LayoutScopeStr);
	O->SetStringField(TEXT("layout_anchor"), LayoutAnchorStr);
	O->SetBoolField(TEXT("layout_hints_ignored"), bLayoutHintsIgnored);
	O->SetBoolField(TEXT("layout_downstream_truncated"), bLayoutDownstreamTruncated);
	O->SetNumberField(TEXT("layout_downstream_extra_nodes"), static_cast<double>(LayoutExtraDownstreamNodes));
	O->SetBoolField(TEXT("layout_local_skipped"), bLayoutLocalSkipped);
	O->SetBoolField(TEXT("layout_applied"), !bLayoutLocalSkipped && (LayoutResult.NodesPositioned > 0 || LayoutResult.NodesMoved > 0));
	O->SetNumberField(TEXT("layout_nodes_positioned"), static_cast<double>(LayoutResult.NodesPositioned));
	O->SetNumberField(TEXT("layout_nodes_moved"), static_cast<double>(LayoutResult.NodesMoved));
	O->SetNumberField(TEXT("layout_nodes_skipped_preserve"), static_cast<double>(LayoutResult.NodesSkippedPreserve));
	O->SetNumberField(TEXT("layout_entry_subgraphs"), static_cast<double>(LayoutResult.EntrySubgraphs));
	O->SetNumberField(TEXT("layout_disconnected_nodes"), static_cast<double>(LayoutResult.DisconnectedNodes));
	O->SetNumberField(TEXT("layout_data_only_nodes_placed"), static_cast<double>(LayoutResult.DataOnlyNodesPlaced));
	O->SetNumberField(TEXT("layout_knots_inserted"), static_cast<double>(LayoutResult.KnotsInserted));
	O->SetNumberField(TEXT("layout_comments_adjusted"), static_cast<double>(LayoutResult.CommentsAdjusted));
	if (LayoutResult.Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnArr;
		for (const FString& W : LayoutResult.Warnings)
		{
			WarnArr.Add(MakeShareable(new FJsonValueString(W)));
		}
		O->SetArrayField(TEXT("layout_warnings"), WarnArr);
	}
	O->SetBoolField(TEXT("formatter_available"), UnrealAiBlueprintFormatterBridge::IsFormatterModuleReady());
	if (bKeepPrefixOnFailure)
	{
		O->SetBoolField(TEXT("ok"), false);
		O->SetStringField(TEXT("status"), TEXT("patch_prefix_kept_failure"));
		TArray<TSharedPtr<FJsonValue>> FailEArr;
		TArray<TSharedPtr<FJsonValue>> FailCodeArr;
		for (const FString& E : PrefixFailureErrors)
		{
			FailEArr.Add(MakeShareable(new FJsonValueString(E)));
			FailCodeArr.Add(MakeShareable(new FJsonValueString(InferBlueprintGraphPatchErrorCode(E))));
		}
		O->SetArrayField(TEXT("errors"), FailEArr);
		O->SetArrayField(TEXT("error_codes"), FailCodeArr);
		AppendErrorsDetailArray(
			PrefixFailureErrors,
			PrefixFailureOpIdx != INDEX_NONE ? PrefixFailureOpIdx : -1,
			nullptr,
			O);
		if (PrefixFailureOpIdx != INDEX_NONE)
		{
			O->SetNumberField(TEXT("failed_op_index"), PrefixFailureOpIdx);
			AppendFailedOpSnippetToPayload(OpsArr, PrefixFailureOpIdx, O);
		}
		if (LastPatchErrGuidFoundInGraphs.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Gf;
			for (const FString& S : LastPatchErrGuidFoundInGraphs)
			{
				Gf.Add(MakeShareable(new FJsonValueString(S)));
			}
			O->SetArrayField(TEXT("guid_found_in_graphs"), Gf);
		}
		AppendConnectAvailablePinsJson(O, PatchErrConnectFromNode, PatchErrConnectToNode);
		if (PrefixFailureErrors.Num() > 0)
		{
			AppendBlueprintGraphPatchSuggestedCorrectCall(Path, GraphName, PrefixFailureErrors[0], O);
		}
		O->SetStringField(
			TEXT("note"),
			TEXT(
				"Earlier ops in this batch were kept; a later op failed. Send a follow-up blueprint_graph_patch to finish wiring, or turn off \"Graph patch: keep successful ops on failure\" in plugin settings for strict rollback."));
		FUnrealAiToolInvocationResult RPartial;
		RPartial.bOk = false;
		RPartial.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
		RPartial.ContentForModel = RPartial.ErrorMessage;
		const FString MdPartial = FString::Printf(
			TEXT("### blueprint_graph_patch (partial)\n- Ops applied before failure: %d\n"),
			Applied.Num());
		RPartial.EditorPresentation =
			UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, GraphName, MdPartial);
		return RPartial;
	}

	const FString Md = FString::Printf(TEXT("### blueprint_graph_patch\n- Ops applied: %d\n"), Applied.Num());
	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, GraphName, Md));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGraphListPins(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiBlueprintGraphPatchPriv;
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required"));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	if (GraphName.IsEmpty())
	{
		GraphName = TEXT("EventGraph");
	}
	FString NodeRef;
	Args->TryGetStringField(TEXT("node_ref"), NodeRef);
	if (NodeRef.IsEmpty())
	{
		Args->TryGetStringField(TEXT("guid"), NodeRef);
	}
	if (NodeRef.IsEmpty())
	{
		return UnrealAiToolJson::Error(
			TEXT("node_ref or guid is required: pass the node's graph GUID from blueprint_graph_introspect (node_guid) or blueprint_graph_patch applied[].node_guid. "
				 "Ephemeral patch_id strings from a prior patch only work inside the same blueprint_graph_patch call, not as node_ref here."));
	}
	FString BpLoadErr;
	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path, &BpLoadErr);
	if (!BP)
	{
		return UnrealAiToolJson::Error(BpLoadErr.IsEmpty() ? TEXT("Could not load Blueprint") : BpLoadErr);
	}
	UEdGraph* Graph = UnrealAiBlueprintTools_FindGraphByName(BP, GraphName);
	if (!Graph)
	{
		return UnrealAiToolJson::Error(TEXT("Graph not found"));
	}
	FGuid NodeGuid;
	FString CanonicalLex;
	if (!UnrealAiTryParseBlueprintGraphNodeGuid(NodeRef, NodeGuid, &CanonicalLex))
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("node_ref must be a graph node GUID (dashed UUID, 32-hex compact, or guid:...) from blueprint_graph_introspect or blueprint_graph_patch applied[].node_guid after a real apply. Ephemeral patch_id strings do not work here. Got: %s"),
			*NodeRef));
	}
	const FString RequestedGraphName = GraphName;
	UEdGraph* ResolvedGraph = Graph;
	FString ResolvedGraphName = GraphName;
	UEdGraphNode* N = FindNodeByGraphGuid(Graph, NodeGuid);
	if (!N)
	{
		TArray<FString> HitGraphs;
		UnrealAiBlueprintTools_FindGraphNamesContainingNodeGuid(BP, NodeGuid, HitGraphs);
		if (HitGraphs.Num() == 1)
		{
			if (UEdGraph* GOther = UnrealAiBlueprintTools_FindGraphByName(BP, HitGraphs[0]))
			{
				ResolvedGraph = GOther;
				ResolvedGraphName = HitGraphs[0];
				N = FindNodeByGraphGuid(ResolvedGraph, NodeGuid);
			}
		}
		else if (HitGraphs.Num() > 1)
		{
			return UnrealAiToolJson::Error(FString::Printf(
				TEXT("Ambiguous node_guid across multiple graphs: %s. Found in: %s"),
				*CanonicalLex,
				*FString::Join(HitGraphs, TEXT(", "))));
		}
	}
	if (!N)
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("Node guid not found in this Blueprint: %s (requested graph: %s). Do not reuse node_guid from validate_only create_node (transient nodes are removed)."),
			*CanonicalLex,
			*RequestedGraphName));
	}
	TArray<TSharedPtr<FJsonValue>> Pins;
	AppendPinsJson(N, Pins);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("graph_name"), ResolvedGraphName);
	O->SetStringField(TEXT("node_guid"), LexToString(N->NodeGuid));
	O->SetStringField(TEXT("k2_class"), N->GetClass()->GetPathName());
	O->SetArrayField(TEXT("pins"), Pins);
	O->SetStringField(TEXT("requested_graph_name"), RequestedGraphName);
	if (!ResolvedGraphName.Equals(RequestedGraphName, ESearchCase::CaseSensitive))
	{
		O->SetStringField(
			TEXT("note"),
			FString::Printf(
				TEXT("Node was found in graph \"%s\" (not \"%s\"). Use that graph_name for blueprint_graph_patch if you need to edit wires on this node."),
				*ResolvedGraphName,
				*RequestedGraphName));
	}
	return UnrealAiToolJson::Ok(O);
}
