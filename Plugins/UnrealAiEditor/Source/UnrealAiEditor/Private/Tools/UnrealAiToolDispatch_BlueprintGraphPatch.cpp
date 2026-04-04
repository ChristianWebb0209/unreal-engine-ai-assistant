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

namespace UnrealAiBlueprintGraphPatchPriv
{
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

	static bool TryCreateLink(UEdGraphPin* PA, UEdGraphPin* PB, const UEdGraphSchema_K2* Schema)
	{
		if (!PA || !PB || !Schema)
		{
			return false;
		}
		if (Schema->TryCreateConnection(PA, PB))
		{
			return true;
		}
		return Schema->TryCreateConnection(PB, PA);
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
		FString& Err)
	{
		FString NodePart = NodePartIn;
		NodePart.TrimStartAndEndInline();
		if (NodePart.Contains(TEXT("__UAI_G_")))
		{
			Err = FString::Printf(
				TEXT("T3D placeholder in node ref \"%s\". __UAI_G_NNNNNN__ is only for blueprint_graph_import_t3d. "
					 "Use guid:<uuid> from blueprint_export_ir / blueprint_graph_introspect, or patch_id from this ops[] batch."),
				*NodePartIn);
			return nullptr;
		}
		if (NodePart.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
		{
			const FString GuidBody = NodePart.Mid(5).TrimStartAndEnd();
			// T3D authoring tokens are not valid FGuids; catch early with one recovery story.
			if (GuidBody.Contains(TEXT("__UAI_G_")))
			{
				Err = FString::Printf(
					TEXT("T3D placeholder in graph_patch node ref: %s. __UAI_G_NNNNNN__ is only for blueprint_t3d_preflight_validate / blueprint_graph_import_t3d. "
						 "Use guid:<real-uuid> from blueprint_graph_introspect (or blueprint_export_ir node_guid), or patch_id from this ops[] batch."),
					*NodePart);
				return nullptr;
			}
			FGuid G;
			if (!FGuid::Parse(GuidBody, G))
			{
				Err = FString::Printf(TEXT("Invalid guid ref: %s"), *NodePart);
				return nullptr;
			}
			if (UEdGraphNode* Found = FindNodeByGraphGuid(Graph, G))
			{
				return Found;
			}
			Err = FString::Printf(TEXT("Node guid not found in graph: %s"), *NodePart);
			return nullptr;
		}
		if (UEdGraphNode* const* Found = PatchMap.Find(NodePart))
		{
			return *Found;
		}
		FGuid BareGuid;
		if (FGuid::Parse(NodePart, BareGuid))
		{
			if (UEdGraphNode* GNode = FindNodeByGraphGuid(Graph, BareGuid))
			{
				return GNode;
			}
			Err = FString::Printf(TEXT("Node guid not found in graph: %s"), *NodePart);
			return nullptr;
		}
		Err = FString::Printf(
			TEXT("Unknown node ref \"%s\" — use a patch_id from this batch, guid:..., or export node_guid"),
			*NodePart);
		return nullptr;
	}

	static UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName)
	{
		if (!Node || PinName.IsEmpty())
		{
			return nullptr;
		}
		const FString Nrm = NormalizePinToken(PinName);
		for (UEdGraphPin* P : Node->Pins)
		{
			if (!P)
			{
				continue;
			}
			if (P->PinName.ToString().Equals(Nrm, ESearchCase::IgnoreCase)
				|| P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase)
				|| P->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				return P;
			}
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
		return nullptr;
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
		Op->TryGetStringField(TEXT("k2_class"), K2ClassPath);
		K2ClassPath.TrimStartAndEndInline();
		if (K2ClassPath.IsEmpty())
		{
			OutErrors.Add(TEXT("create_node requires k2_class (e.g. /Script/BlueprintGraph.K2Node_IfThenElse)"));
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
				Hint = TEXT(" Literal ints: K2Node_CallFunction with KismetMathLibrary.MakeLiteralInt, or set_pin_default on an int input.");
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
				OutErrors.Add(FString::Printf(
					TEXT("Function not found on %s (after resolving name '%s'). Use class_path of the declaring UClass (e.g. /Script/Engine.Actor + GetActorLocation; /Script/Engine.KismetMathLibrary + RandomFloatInRange; /Script/Engine.KismetSystemLibrary + Delay)."),
					*ClsPath,
					*FnName));
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
				OutErrors.Add(FString::Printf(
					TEXT("Function not found on %s (after resolving name '%s'). See KismetMathLibrary for math, Actor for GetActorLocation, KismetSystemLibrary for Delay/PrintString."),
					*ClsPath,
					*FnName));
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
					"event_override requires function_name and outer_class_path (e.g. ReceiveBeginPlay + /Script/Engine.Actor). Common lifecycle events default outer_class_path when omitted."));
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
} // namespace UnrealAiBlueprintGraphPatchPriv

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGraphPatch(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiBlueprintGraphPatchPriv;
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(Args);
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
	const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("ops"), OpsArr) || !OpsArr || OpsArr->Num() == 0)
	{
		return UnrealAiToolJson::Error(TEXT("ops array is required with at least one operation"));
	}

	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint (check path and GeneratedClass)"));
	}
	UEdGraph* Graph = UnrealAiBlueprintTools_FindGraphByName(BP, GraphName);
	if (!Graph)
	{
		return UnrealAiToolJson::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	bool bCompile = true;
	Args->TryGetBoolField(TEXT("compile"), bCompile);

	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnBpGraphPatch", "Unreal AI: blueprint_graph_patch"));
	BP->Modify();
	Graph->Modify();

	TMap<FString, UEdGraphNode*> PatchMap;
	TArray<FString> Errors;
	TArray<TSharedPtr<FJsonValue>> Applied;
	TArray<UEdGraphNode*> LayoutNodes;
	TArray<FUnrealBlueprintIrNodeLayoutHint> LayoutHints;
	TArray<FCommentReflowJob> CommentReflowJobs;

	bool bAutoLayout = true;
	if (Args->HasField(TEXT("auto_layout")))
	{
		Args->TryGetBoolField(TEXT("auto_layout"), bAutoLayout);
	}
	FString LayoutScopeStr = TEXT("patched_nodes");
	Args->TryGetStringField(TEXT("layout_scope"), LayoutScopeStr);
	LayoutScopeStr.TrimStartAndEndInline();
	if (LayoutScopeStr.IsEmpty())
	{
		LayoutScopeStr = TEXT("patched_nodes");
	}
	const bool bLayoutFullGraph = LayoutScopeStr.Equals(TEXT("full_graph"), ESearchCase::IgnoreCase);

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	for (const TSharedPtr<FJsonValue>& OpVal : *OpsArr)
	{
		const TSharedPtr<FJsonObject>* OpObj = nullptr;
		if (!OpVal->TryGetObject(OpObj) || !OpObj || !(*OpObj).IsValid())
		{
			Errors.Add(TEXT("Each ops[] entry must be an object"));
			continue;
		}
		const TSharedPtr<FJsonObject>& Op = *OpObj;
		FString OpName;
		Op->TryGetStringField(TEXT("op"), OpName);
		OpName.TrimStartAndEndInline();
		if (OpName.IsEmpty())
		{
			Errors.Add(TEXT("ops[].op is required"));
			continue;
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
				continue;
			}
			FEdGraphPinType Pt;
			if (!UnrealAiBlueprintTools_TryParsePinTypeFromString(Tstr, Pt))
			{
				Errors.Add(FString::Printf(
					TEXT("add_variable: unknown or unsupported type \"%s\" (try int, float, bool, name, text, or /Script/... class path)"),
					*Tstr));
				continue;
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
				continue;
			}
			UEdGraphNode* NA = ResolveNodePart(NFrom, PatchMap, Graph, Err);
			if (!NA)
			{
				Errors.Add(FString::Printf(TEXT("connect from: %s"), *Err));
				continue;
			}
			UEdGraphNode* NB = ResolveNodePart(NTo, PatchMap, Graph, Err);
			if (!NB)
			{
				Errors.Add(FString::Printf(TEXT("connect to: %s"), *Err));
				continue;
			}
			UEdGraphPin* PA = FindPin(NA, PFrom);
			UEdGraphPin* PB = FindPin(NB, PTo);
			if (!PA || !PB)
			{
				Errors.Add(FString::Printf(TEXT("connect pin not found (%s or %s)"), *FromS, *ToS));
				continue;
			}
			if (!TryCreateLink(PA, PB, Schema))
			{
				Errors.Add(FString::Printf(TEXT("Could not connect %s -> %s (type/direction)"), *FromS, *ToS));
				continue;
			}
			TSharedPtr<FJsonObject> Rec = MakeShared<FJsonObject>();
			Rec->SetStringField(TEXT("op"), TEXT("connect"));
			Rec->SetStringField(TEXT("from"), FromS);
			Rec->SetStringField(TEXT("to"), ToS);
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
				continue;
			}
			if (Val.IsEmpty())
			{
				Errors.Add(TEXT("set_pin_default requires value"));
				continue;
			}
			UEdGraphNode* N = ResolveNodePart(NPart, PatchMap, Graph, Err);
			if (!N)
			{
				Errors.Add(Err);
				continue;
			}
			UEdGraphPin* P = FindPin(N, PName);
			if (!P)
			{
				Errors.Add(FString::Printf(TEXT("set_pin_default: pin %s not on node"), *PName));
				continue;
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
				continue;
			}
			UEdGraphNode* NA = ResolveNodePart(NFrom, PatchMap, Graph, Err);
			if (!NA)
			{
				Errors.Add(FString::Printf(TEXT("break_link from: %s"), *Err));
				continue;
			}
			UEdGraphNode* NB = ResolveNodePart(NTo, PatchMap, Graph, Err);
			if (!NB)
			{
				Errors.Add(FString::Printf(TEXT("break_link to: %s"), *Err));
				continue;
			}
			UEdGraphPin* PA = FindPin(NA, PFrom);
			UEdGraphPin* PB = FindPin(NB, PTo);
			if (!PA || !PB)
			{
				Errors.Add(TEXT("break_link: pin not found"));
				continue;
			}
			if (!TryBreakPinLink(PA, PB, Schema))
			{
				Errors.Add(TEXT("break_link: no link between those pins"));
				continue;
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
				continue;
			}
			UEdGraphNode* UpNode = ResolveNodePart(NFrom, PatchMap, Graph, Err);
			if (!UpNode)
			{
				Errors.Add(FString::Printf(TEXT("splice_on_link from: %s"), *Err));
				continue;
			}
			UEdGraphNode* DownNode = ResolveNodePart(NTo, PatchMap, Graph, Err);
			if (!DownNode)
			{
				Errors.Add(FString::Printf(TEXT("splice_on_link to: %s"), *Err));
				continue;
			}
			UEdGraphNode* const* MidPtr = PatchMap.Find(InsPid);
			if (!MidPtr || !*MidPtr)
			{
				Errors.Add(TEXT("splice_on_link: insert_patch_id not found in this patch batch"));
				continue;
			}
			UEdGraphPin* UpPin = FindPin(UpNode, PFrom);
			UEdGraphPin* DownPin = FindPin(DownNode, PTo);
			UEdGraphPin* MidIn = FindPin(*MidPtr, InPinStr);
			UEdGraphPin* MidOut = FindPin(*MidPtr, OutPinStr);
			if (!UpPin || !DownPin || !MidIn || !MidOut)
			{
				Errors.Add(TEXT("splice_on_link: pin resolution failed (check insert_input_pin / insert_output_pin)"));
				continue;
			}
			if (!TryBreakPinLink(UpPin, DownPin, Schema))
			{
				Errors.Add(TEXT("splice_on_link: no direct link between from and to pins"));
				continue;
			}
			if (!TryCreateLink(UpPin, MidIn, Schema) || !TryCreateLink(MidOut, DownPin, Schema))
			{
				Errors.Add(TEXT("splice_on_link: reconnect failed after break (try manual connect ops)"));
				continue;
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
				continue;
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
				N = ResolveNodePart(Pid, PatchMap, Graph, Err);
			}
			else if (!GuidStr.IsEmpty())
			{
				FString G = GuidStr.TrimStartAndEnd();
				if (!G.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
				{
					G = FString(TEXT("guid:")) + G;
				}
				N = ResolveNodePart(G, PatchMap, Graph, Err);
			}
			if (!N)
			{
				Errors.Add(FString::Printf(TEXT("remove_node: %s"), *Err));
				continue;
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
				N = ResolveNodePart(Pid, PatchMap, Graph, Err);
			}
			else if (!GuidS.IsEmpty())
			{
				FString G = GuidS.TrimStartAndEnd();
				if (!G.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
				{
					G = FString(TEXT("guid:")) + G;
				}
				N = ResolveNodePart(G, PatchMap, Graph, Err);
			}
			if (!N)
			{
				Errors.Add(FString::Printf(TEXT("move_node: %s"), *Err));
				continue;
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
	}

	if (Errors.Num() > 0)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), false);
		O->SetStringField(TEXT("status"), TEXT("patch_errors"));
		TArray<TSharedPtr<FJsonValue>> EArr;
		for (const FString& E : Errors)
		{
			EArr.Add(MakeShareable(new FJsonValueString(E)));
		}
		O->SetArrayField(TEXT("errors"), EArr);
		O->SetArrayField(TEXT("applied_partial"), Applied);
		FUnrealAiToolInvocationResult R;
		R.bOk = false;
		R.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
		R.ContentForModel = R.ErrorMessage;
		return R;
	}

	UnrealAiBlueprintFormatterBridge::EnsureFormatterModuleLoaded(nullptr);
	const FUnrealBlueprintGraphFormatOptions FmtOpts =
		UnrealAiBlueprintTools_MakeFormatOptionsFromSettings(GetDefault<UUnrealAiEditorSettings>());
	FUnrealBlueprintGraphFormatResult LayoutResult;
	if (bAutoLayout)
	{
		if (bLayoutFullGraph)
		{
			LayoutResult = UnrealAiBlueprintFormatterBridge::TryLayoutEntireGraph(Graph, true, FmtOpts);
		}
		else if (LayoutNodes.Num() > 0)
		{
			LayoutResult = UnrealAiBlueprintFormatterBridge::TryLayoutAfterAiIrApply(
				Graph,
				LayoutNodes,
				LayoutHints,
				true,
				FmtOpts);
		}
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
			if (UEdGraphNode* N = ResolveNodePart(Part, PatchMap, Graph, E2))
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
	O->SetBoolField(TEXT("layout_applied"), LayoutResult.NodesPositioned > 0 || LayoutResult.NodesMoved > 0);
	O->SetNumberField(TEXT("layout_nodes_positioned"), static_cast<double>(LayoutResult.NodesPositioned));
	O->SetNumberField(TEXT("layout_nodes_moved"), static_cast<double>(LayoutResult.NodesMoved));
	O->SetNumberField(TEXT("layout_nodes_skipped_preserve"), static_cast<double>(LayoutResult.NodesSkippedPreserve));
	O->SetNumberField(TEXT("layout_entry_subgraphs"), static_cast<double>(LayoutResult.EntrySubgraphs));
	O->SetNumberField(TEXT("layout_disconnected_nodes"), static_cast<double>(LayoutResult.DisconnectedNodes));
	O->SetNumberField(TEXT("layout_data_only_nodes_placed"), static_cast<double>(LayoutResult.DataOnlyNodesPlaced));
	O->SetNumberField(TEXT("layout_knots_inserted"), static_cast<double>(LayoutResult.KnotsInserted));
	O->SetNumberField(TEXT("layout_comments_adjusted"), static_cast<double>(LayoutResult.CommentsAdjusted));
	O->SetBoolField(TEXT("formatter_available"), UnrealAiBlueprintFormatterBridge::IsFormatterModuleReady());
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
			TEXT("node_ref or guid is required: pass the node's graph GUID from blueprint_export_ir (node_guid) or blueprint_graph_patch applied[].node_guid. "
				 "Ephemeral patch_id strings from a prior patch only work inside the same blueprint_graph_patch call, not as node_ref here."));
	}
	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint"));
	}
	UEdGraph* Graph = UnrealAiBlueprintTools_FindGraphByName(BP, GraphName);
	if (!Graph)
	{
		return UnrealAiToolJson::Error(TEXT("Graph not found"));
	}
	TMap<FString, UEdGraphNode*> EmptyPatch;
	FString Err;
	UEdGraphNode* N = nullptr;
	if (NodeRef.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
	{
		N = ResolveNodePart(NodeRef, EmptyPatch, Graph, Err);
	}
	else
	{
		FGuid G;
		const FString Trimmed = NodeRef.TrimStartAndEnd();
		if (FGuid::Parse(Trimmed, G))
		{
			N = ResolveNodePart(FString(TEXT("guid:")) + Trimmed, EmptyPatch, Graph, Err);
		}
		else
		{
			N = nullptr;
			Err = TEXT("node_ref must be a graph node GUID or guid:{uuid} (from blueprint_export_ir / create_node result)");
		}
	}
	if (!N)
	{
		return UnrealAiToolJson::Error(FString::Printf(TEXT("Node not found: %s (%s)"), *NodeRef, *Err));
	}
	TArray<TSharedPtr<FJsonValue>> Pins;
	AppendPinsJson(N, Pins);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("graph_name"), GraphName);
	O->SetStringField(TEXT("node_guid"), LexToString(N->NodeGuid));
	O->SetStringField(TEXT("k2_class"), N->GetClass()->GetPathName());
	O->SetArrayField(TEXT("pins"), Pins);
	return UnrealAiToolJson::Ok(O);
}
