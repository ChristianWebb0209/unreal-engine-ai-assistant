#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

#include "Tools/UnrealAiToolDispatch_MoreAssets.h"
#include "Tools/UnrealAiToolJson.h"

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "K2Node_CustomEvent.h"
#include "Logging/TokenizedMessage.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "UObject/Class.h"
#include "UObject/Field.h"
#include "UObject/UnrealType.h"

namespace UnrealAiBlueprintToolsPriv
{
	struct FIrError
	{
		FString Code;
		FString Path;
		FString Message;
		FString Hint;
	};

	struct FIrVariableDecl
	{
		FString Name;
		FString Type;
	};

	struct FIrNodeDecl
	{
		FString NodeId;
		FString Op;
		FString Name;
		int32 X = 0;
		int32 Y = 0;
		FString ClassPath;
		FString FunctionName;
		FString Variable;
	};

	struct FIrLinkDecl
	{
		FString From;
		FString To;
	};

	struct FIrDefaultDecl
	{
		FString NodeId;
		FString Pin;
		FString Value;
	};

	struct FBlueprintIr
	{
		FString BlueprintPath;
		FString GraphName;
		TArray<FIrVariableDecl> Variables;
		TArray<FIrNodeDecl> Nodes;
		TArray<FIrLinkDecl> Links;
		TArray<FIrDefaultDecl> Defaults;
	};

	static UBlueprint* LoadBlueprint(const FString& Path)
	{
		return LoadObject<UBlueprint>(nullptr, *Path);
	}

	static UEdGraph* FindGraphByName(UBlueprint* BP, const FString& GraphName)
	{
		if (!BP || GraphName.IsEmpty())
		{
			return nullptr;
		}
		const FName GN(*GraphName);
		auto TryList = [&GN, &GraphName](const TArray<UEdGraph*>& List) -> UEdGraph*
		{
			for (UEdGraph* G : List)
			{
				if (G && (G->GetFName() == GN || G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)))
				{
					return G;
				}
			}
			return nullptr;
		};
		if (UEdGraph* G = TryList(BP->UbergraphPages))
		{
			return G;
		}
		if (UEdGraph* G = TryList(BP->FunctionGraphs))
		{
			return G;
		}
		if (UEdGraph* G = TryList(BP->MacroGraphs))
		{
			return G;
		}
		return nullptr;
	}

	static FEdGraphPinType ParsePinType(const FString& TypeStr)
	{
		FEdGraphPinType P;
		const FString T = TypeStr.ToLower();
		if (T == TEXT("bool") || T == TEXT("boolean"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (T == TEXT("int") || T == TEXT("integer"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (T == TEXT("float") || T == TEXT("double"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Real;
			P.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		}
		else if (T == TEXT("string"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		else if (T == TEXT("vector"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Struct;
			P.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		}
		else if (T == TEXT("rotator"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Struct;
			P.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		}
		else
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		return P;
	}

	static void AddError(TArray<FIrError>& Errors, const TCHAR* Code, const FString& Path, const FString& Msg, const FString& Hint)
	{
		FIrError E;
		E.Code = Code;
		E.Path = Path;
		E.Message = Msg;
		E.Hint = Hint;
		Errors.Add(MoveTemp(E));
	}

	static FUnrealAiToolInvocationResult MakeIrErrorResult(const TCHAR* Status, const TCHAR* Headline, const TArray<FIrError>& Errors)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), false);
		O->SetStringField(TEXT("status"), Status);
		O->SetStringField(TEXT("error"), Headline);

		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FIrError& E : Errors)
		{
			TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("code"), E.Code);
			J->SetStringField(TEXT("path"), E.Path);
			J->SetStringField(TEXT("message"), E.Message);
			if (!E.Hint.IsEmpty())
			{
				J->SetStringField(TEXT("hint"), E.Hint);
			}
			Arr.Add(MakeShareable(new FJsonValueObject(J.ToSharedRef())));
		}
		O->SetArrayField(TEXT("errors"), Arr);

		FUnrealAiToolInvocationResult R;
		R.bOk = false;
		R.ErrorMessage = Headline;
		R.ContentForModel = UnrealAiToolJson::SerializeObject(O);
		return R;
	}

	static bool TryParseIr(const TSharedPtr<FJsonObject>& Args, FBlueprintIr& OutIr, TArray<FIrError>& OutErrors)
	{
		if (!Args.IsValid())
		{
			AddError(OutErrors, TEXT("invalid_args"), TEXT("$"), TEXT("Tool arguments must be an object"), TEXT(""));
			return false;
		}
		if (!Args->TryGetStringField(TEXT("blueprint_path"), OutIr.BlueprintPath) || OutIr.BlueprintPath.IsEmpty())
		{
			AddError(
				OutErrors,
				TEXT("missing_required"),
				TEXT("$.blueprint_path"),
				TEXT("blueprint_path is required"),
				TEXT("Pass a valid object path, e.g. /Game/BP_MyActor.BP_MyActor"));
		}
		if (!Args->TryGetStringField(TEXT("graph_name"), OutIr.GraphName) || OutIr.GraphName.IsEmpty())
		{
			OutIr.GraphName = TEXT("EventGraph");
		}

		const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
		if (Args->TryGetArrayField(TEXT("variables"), Variables) && Variables)
		{
			for (int32 i = 0; i < Variables->Num(); ++i)
			{
				const TSharedPtr<FJsonValue>& V = (*Variables)[i];
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
				{
					AddError(OutErrors, TEXT("invalid_type"), FString::Printf(TEXT("$.variables[%d]"), i), TEXT("Expected object"), TEXT(""));
					continue;
				}
				FIrVariableDecl D;
				(*O)->TryGetStringField(TEXT("name"), D.Name);
				(*O)->TryGetStringField(TEXT("type"), D.Type);
				if (D.Name.IsEmpty() || D.Type.IsEmpty())
				{
					AddError(
						OutErrors,
						TEXT("missing_required"),
						FString::Printf(TEXT("$.variables[%d]"), i),
						TEXT("variables entries require name and type"),
						TEXT("Use {\"name\":\"Health\",\"type\":\"float\"}"));
					continue;
				}
				OutIr.Variables.Add(MoveTemp(D));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Args->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes || Nodes->Num() == 0)
		{
			AddError(OutErrors, TEXT("missing_required"), TEXT("$.nodes"), TEXT("nodes array is required"), TEXT("Add at least one node declaration"));
		}
		else
		{
			for (int32 i = 0; i < Nodes->Num(); ++i)
			{
				const TSharedPtr<FJsonValue>& V = (*Nodes)[i];
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
				{
					AddError(OutErrors, TEXT("invalid_type"), FString::Printf(TEXT("$.nodes[%d]"), i), TEXT("Expected object"), TEXT(""));
					continue;
				}
				FIrNodeDecl D;
				(*O)->TryGetStringField(TEXT("node_id"), D.NodeId);
				(*O)->TryGetStringField(TEXT("op"), D.Op);
				(*O)->TryGetStringField(TEXT("name"), D.Name);
				(*O)->TryGetStringField(TEXT("class_path"), D.ClassPath);
				(*O)->TryGetStringField(TEXT("function_name"), D.FunctionName);
				(*O)->TryGetStringField(TEXT("variable"), D.Variable);
				(*O)->TryGetNumberField(TEXT("x"), D.X);
				(*O)->TryGetNumberField(TEXT("y"), D.Y);
				if (D.NodeId.IsEmpty() || D.Op.IsEmpty())
				{
					AddError(
						OutErrors,
						TEXT("missing_required"),
						FString::Printf(TEXT("$.nodes[%d]"), i),
						TEXT("nodes entries require node_id and op"),
						TEXT("Use {\"node_id\":\"n1\",\"op\":\"branch\"}"));
					continue;
				}
				OutIr.Nodes.Add(MoveTemp(D));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
		if (Args->TryGetArrayField(TEXT("links"), Links) && Links)
		{
			for (int32 i = 0; i < Links->Num(); ++i)
			{
				const TSharedPtr<FJsonValue>& V = (*Links)[i];
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
				{
					AddError(OutErrors, TEXT("invalid_type"), FString::Printf(TEXT("$.links[%d]"), i), TEXT("Expected object"), TEXT(""));
					continue;
				}
				FIrLinkDecl D;
				(*O)->TryGetStringField(TEXT("from"), D.From);
				(*O)->TryGetStringField(TEXT("to"), D.To);
				if (D.From.IsEmpty() || D.To.IsEmpty())
				{
					AddError(
						OutErrors,
						TEXT("missing_required"),
						FString::Printf(TEXT("$.links[%d]"), i),
						TEXT("links entries require from and to"),
						TEXT("Use node.pin syntax, e.g. begin_play.Then -> branch.Exec"));
					continue;
				}
				OutIr.Links.Add(MoveTemp(D));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Defaults = nullptr;
		if (Args->TryGetArrayField(TEXT("defaults"), Defaults) && Defaults)
		{
			for (int32 i = 0; i < Defaults->Num(); ++i)
			{
				const TSharedPtr<FJsonValue>& V = (*Defaults)[i];
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
				{
					AddError(OutErrors, TEXT("invalid_type"), FString::Printf(TEXT("$.defaults[%d]"), i), TEXT("Expected object"), TEXT(""));
					continue;
				}
				FIrDefaultDecl D;
				(*O)->TryGetStringField(TEXT("node_id"), D.NodeId);
				(*O)->TryGetStringField(TEXT("pin"), D.Pin);
				(*O)->TryGetStringField(TEXT("value"), D.Value);
				if (D.NodeId.IsEmpty() || D.Pin.IsEmpty())
				{
					AddError(
						OutErrors,
						TEXT("missing_required"),
						FString::Printf(TEXT("$.defaults[%d]"), i),
						TEXT("defaults entries require node_id and pin"),
						TEXT("Use {\"node_id\":\"print\",\"pin\":\"InString\",\"value\":\"hello\"}"));
					continue;
				}
				OutIr.Defaults.Add(MoveTemp(D));
			}
		}

		return OutErrors.Num() == 0;
	}

	static bool SplitNodePin(const FString& S, FString& OutNodeId, FString& OutPinName)
	{
		int32 Dot = INDEX_NONE;
		if (!S.FindLastChar(TEXT('.'), Dot) || Dot <= 0 || Dot >= S.Len() - 1)
		{
			return false;
		}
		OutNodeId = S.Left(Dot);
		OutPinName = S.Mid(Dot + 1);
		return true;
	}

	static FString VarTypeToSimpleString(const FEdGraphPinType& T)
	{
		if (T.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return TEXT("bool");
		}
		if (T.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			return TEXT("int");
		}
		if (T.PinCategory == UEdGraphSchema_K2::PC_Real)
		{
			return T.PinSubCategory == UEdGraphSchema_K2::PC_Float ? TEXT("float") : TEXT("double");
		}
		if (T.PinCategory == UEdGraphSchema_K2::PC_String)
		{
			return TEXT("string");
		}
		if (T.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			if (T.PinSubCategoryObject == TBaseStructure<FVector>::Get())
			{
				return TEXT("vector");
			}
			if (T.PinSubCategoryObject == TBaseStructure<FRotator>::Get())
			{
				return TEXT("rotator");
			}
		}
		return T.PinCategory.ToString();
	}

	static FString NodeIdString(UEdGraphNode* N)
	{
		return N ? LexToString(N->NodeGuid) : FString();
	}

	static void ExportNodeFields(UEdGraphNode* N, TSharedPtr<FJsonObject>& OutObj, FString& OutOp)
	{
		OutObj = MakeShared<FJsonObject>();
		OutObj->SetNumberField(TEXT("x"), static_cast<double>(N->NodePosX));
		OutObj->SetNumberField(TEXT("y"), static_cast<double>(N->NodePosY));

		if (UK2Node_Event* Ev = Cast<UK2Node_Event>(N))
		{
			const FName Mn = Ev->EventReference.GetMemberName();
			if (Mn == FName(TEXT("ReceiveBeginPlay")))
			{
				OutOp = TEXT("event_begin_play");
				OutObj->SetStringField(TEXT("op"), OutOp);
				return;
			}
		}
		if (UK2Node_CustomEvent* Ce = Cast<UK2Node_CustomEvent>(N))
		{
			OutOp = TEXT("custom_event");
			OutObj->SetStringField(TEXT("op"), OutOp);
			OutObj->SetStringField(TEXT("name"), Ce->CustomFunctionName.ToString());
			return;
		}
		if (Cast<UK2Node_IfThenElse>(N))
		{
			OutOp = TEXT("branch");
			OutObj->SetStringField(TEXT("op"), OutOp);
			return;
		}
		if (Cast<UK2Node_ExecutionSequence>(N))
		{
			OutOp = TEXT("sequence");
			OutObj->SetStringField(TEXT("op"), OutOp);
			return;
		}
		if (UK2Node_CallFunction* Cf = Cast<UK2Node_CallFunction>(N))
		{
			if (UFunction* Fn = Cf->GetTargetFunction())
			{
				if (Fn->GetOuterUClass() == UKismetSystemLibrary::StaticClass()
					&& Fn->GetFName() == FName(TEXT("Delay")))
				{
					OutOp = TEXT("delay");
					OutObj->SetStringField(TEXT("op"), OutOp);
					return;
				}
				OutOp = TEXT("call_function");
				OutObj->SetStringField(TEXT("op"), OutOp);
				if (UClass* Cls = Fn->GetOuterUClass())
				{
					OutObj->SetStringField(TEXT("class_path"), Cls->GetPathName());
				}
				OutObj->SetStringField(TEXT("function_name"), Fn->GetName());
				return;
			}
		}
		if (UK2Node_VariableGet* Vg = Cast<UK2Node_VariableGet>(N))
		{
			OutOp = TEXT("get_variable");
			OutObj->SetStringField(TEXT("op"), OutOp);
			OutObj->SetStringField(TEXT("variable"), Vg->VariableReference.GetMemberName().ToString());
			return;
		}
		if (UK2Node_VariableSet* Vs = Cast<UK2Node_VariableSet>(N))
		{
			OutOp = TEXT("set_variable");
			OutObj->SetStringField(TEXT("op"), OutOp);
			OutObj->SetStringField(TEXT("variable"), Vs->VariableReference.GetMemberName().ToString());
			return;
		}
		if (UK2Node_DynamicCast* Dc = Cast<UK2Node_DynamicCast>(N))
		{
			OutOp = TEXT("dynamic_cast");
			OutObj->SetStringField(TEXT("op"), OutOp);
			UClass* Tgt = Dc->TargetType;
			if (Tgt)
			{
				OutObj->SetStringField(TEXT("class_path"), Tgt->GetPathName());
			}
			return;
		}

		OutOp = TEXT("unknown");
		OutObj->SetStringField(TEXT("op"), OutOp);
		OutObj->SetStringField(TEXT("class"), N->GetClass()->GetName());
		OutObj->SetStringField(TEXT("title"), N->GetNodeTitle(ENodeTitleType::ListView).ToString());
	}

	static UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName)
	{
		if (!Node || PinName.IsEmpty())
		{
			return nullptr;
		}
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P && (P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase)
					  || P->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase)))
			{
				return P;
			}
		}
		return nullptr;
	}

	static UEdGraphNode* CreateNodeFromDecl(UBlueprint* BP, UEdGraph* Graph, const FIrNodeDecl& D, TArray<FIrError>& Errors)
	{
		if (!BP || !Graph)
		{
			return nullptr;
		}

		const FString Op = D.Op.ToLower();
		UEdGraphNode* Created = nullptr;

		if (Op == TEXT("event_begin_play"))
		{
			UK2Node_Event* N = NewObject<UK2Node_Event>(Graph);
			N->EventReference.SetExternalMember(FName(TEXT("ReceiveBeginPlay")), AActor::StaticClass());
			N->bOverrideFunction = true;
			Graph->AddNode(N, true, false);
			N->NodePosX = D.X;
			N->NodePosY = D.Y;
			N->CreateNewGuid();
			N->PostPlacedNewNode();
			N->AllocateDefaultPins();
			Created = N;
		}
		else if (Op == TEXT("custom_event"))
		{
			if (D.Name.IsEmpty())
			{
				AddError(
					Errors,
					TEXT("invalid_node"),
					FString::Printf(TEXT("$.nodes[%s]"), *D.NodeId),
					TEXT("custom_event requires name (function name)"),
					TEXT(""));
				return nullptr;
			}
			UK2Node_CustomEvent* N = NewObject<UK2Node_CustomEvent>(Graph);
			Graph->AddNode(N, true, false);
			N->NodePosX = D.X;
			N->NodePosY = D.Y;
			N->CreateNewGuid();
			N->CustomFunctionName = FName(*D.Name);
			N->PostPlacedNewNode();
			N->AllocateDefaultPins();
			Created = N;
		}
		else if (Op == TEXT("branch"))
		{
			UK2Node_IfThenElse* N = NewObject<UK2Node_IfThenElse>(Graph);
			Graph->AddNode(N, true, false);
			N->NodePosX = D.X;
			N->NodePosY = D.Y;
			N->CreateNewGuid();
			N->PostPlacedNewNode();
			N->AllocateDefaultPins();
			Created = N;
		}
		else if (Op == TEXT("sequence"))
		{
			UK2Node_ExecutionSequence* N = NewObject<UK2Node_ExecutionSequence>(Graph);
			Graph->AddNode(N, true, false);
			N->NodePosX = D.X;
			N->NodePosY = D.Y;
			N->CreateNewGuid();
			N->PostPlacedNewNode();
			N->AllocateDefaultPins();
			Created = N;
		}
		else if (Op == TEXT("call_function"))
		{
			if (D.ClassPath.IsEmpty() || D.FunctionName.IsEmpty())
			{
				AddError(
					Errors,
					TEXT("invalid_node"),
					FString::Printf(TEXT("$.nodes[%s]"), *D.NodeId),
					TEXT("call_function requires class_path and function_name"),
					TEXT("Example: class_path=/Script/Engine.KismetSystemLibrary, function_name=PrintString"));
				return nullptr;
			}
			UClass* Class = LoadObject<UClass>(nullptr, *D.ClassPath);
			UFunction* Fn = Class ? Class->FindFunctionByName(FName(*D.FunctionName)) : nullptr;
			if (!Fn)
			{
				AddError(
					Errors,
					TEXT("symbol_not_found"),
					FString::Printf(TEXT("$.nodes[%s]"), *D.NodeId),
					TEXT("Function not found on class"),
					TEXT("Verify class_path and function_name"));
				return nullptr;
			}
			UK2Node_CallFunction* N = NewObject<UK2Node_CallFunction>(Graph);
			Graph->AddNode(N, true, false);
			N->NodePosX = D.X;
			N->NodePosY = D.Y;
			N->CreateNewGuid();
			N->SetFromFunction(Fn);
			N->PostPlacedNewNode();
			N->AllocateDefaultPins();
			Created = N;
		}
		else if (Op == TEXT("delay"))
		{
			UK2Node_CallFunction* N = NewObject<UK2Node_CallFunction>(Graph);
			Graph->AddNode(N, true, false);
			N->NodePosX = D.X;
			N->NodePosY = D.Y;
			N->CreateNewGuid();
			N->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("Delay")));
			N->PostPlacedNewNode();
			N->AllocateDefaultPins();
			Created = N;
		}
		else if (Op == TEXT("get_variable"))
		{
			if (D.Variable.IsEmpty())
			{
				AddError(Errors, TEXT("invalid_node"), D.NodeId, TEXT("get_variable requires variable"), TEXT(""));
				return nullptr;
			}
			UK2Node_VariableGet* N = NewObject<UK2Node_VariableGet>(Graph);
			Graph->AddNode(N, true, false);
			N->NodePosX = D.X;
			N->NodePosY = D.Y;
			N->CreateNewGuid();
			N->VariableReference.SetSelfMember(FName(*D.Variable));
			N->PostPlacedNewNode();
			N->AllocateDefaultPins();
			N->ReconstructNode();
			Created = N;
		}
		else if (Op == TEXT("set_variable"))
		{
			if (D.Variable.IsEmpty())
			{
				AddError(Errors, TEXT("invalid_node"), D.NodeId, TEXT("set_variable requires variable"), TEXT(""));
				return nullptr;
			}
			UK2Node_VariableSet* N = NewObject<UK2Node_VariableSet>(Graph);
			Graph->AddNode(N, true, false);
			N->NodePosX = D.X;
			N->NodePosY = D.Y;
			N->CreateNewGuid();
			N->VariableReference.SetSelfMember(FName(*D.Variable));
			N->PostPlacedNewNode();
			N->AllocateDefaultPins();
			N->ReconstructNode();
			Created = N;
		}
		else if (Op == TEXT("dynamic_cast"))
		{
			if (D.ClassPath.IsEmpty())
			{
				AddError(Errors, TEXT("invalid_node"), D.NodeId, TEXT("dynamic_cast requires class_path"), TEXT(""));
				return nullptr;
			}
			UClass* TargetClass = LoadObject<UClass>(nullptr, *D.ClassPath);
			if (!TargetClass)
			{
				AddError(Errors, TEXT("symbol_not_found"), D.NodeId, TEXT("Cast target class not found"), TEXT("Verify class_path"));
				return nullptr;
			}
			UK2Node_DynamicCast* N = NewObject<UK2Node_DynamicCast>(Graph);
			Graph->AddNode(N, true, false);
			N->NodePosX = D.X;
			N->NodePosY = D.Y;
			N->CreateNewGuid();
			N->TargetType = TargetClass;
			N->PostPlacedNewNode();
			N->AllocateDefaultPins();
			Created = N;
		}
		else
		{
			AddError(
				Errors,
				TEXT("unsupported_op"),
				FString::Printf(TEXT("$.nodes[%s].op"), *D.NodeId),
				FString::Printf(TEXT("Unsupported op '%s'"), *D.Op),
				TEXT("Supported ops: event_begin_play, custom_event, branch, sequence, call_function, delay, get_variable, set_variable, dynamic_cast"));
			return nullptr;
		}

		return Created;
	}
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintCompile(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required"));
	}
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint"));
	}
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), BP->Status != BS_Error);
	O->SetNumberField(TEXT("blueprint_status"), static_cast<double>(static_cast<int32>(BP->Status)));
	TArray<TSharedPtr<FJsonValue>> Msgs;
	int32 ErrCount = 0;
	int32 WarnCount = 0;
	for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
	{
		const EMessageSeverity::Type Sev = Msg->GetSeverity();
		if (Sev == EMessageSeverity::Error)
		{
			++ErrCount;
		}
		else if (Sev == EMessageSeverity::Warning || Sev == EMessageSeverity::PerformanceWarning)
		{
			++WarnCount;
		}
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("text"), Msg->ToText().ToString());
		M->SetNumberField(TEXT("severity"), static_cast<double>(static_cast<int32>(Sev)));
		Msgs.Add(MakeShareable(new FJsonValueObject(M.ToSharedRef())));
	}
	O->SetArrayField(TEXT("compiler_messages"), Msgs);
	O->SetNumberField(TEXT("compiler_error_count"), static_cast<double>(ErrCount));
	O->SetNumberField(TEXT("compiler_warning_count"), static_cast<double>(WarnCount));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintExportIr(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiBlueprintToolsPriv;
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required"));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	UBlueprint* BP = LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint"));
	}
	UEdGraph* Graph = nullptr;
	if (!GraphName.IsEmpty())
	{
		Graph = FindGraphByName(BP, GraphName);
	}
	else
	{
		Graph = BP->UbergraphPages.Num() > 0 ? BP->UbergraphPages[0] : nullptr;
	}
	if (!Graph)
	{
		return UnrealAiToolJson::Error(TEXT("Graph not found"));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("blueprint_path"), Path);
	Root->SetStringField(TEXT("graph_name"), Graph->GetName());

	TArray<TSharedPtr<FJsonValue>> VarArr;
	for (const FBPVariableDescription& Vd : BP->NewVariables)
	{
		TSharedPtr<FJsonObject> Vo = MakeShared<FJsonObject>();
		Vo->SetStringField(TEXT("name"), Vd.VarName.ToString());
		Vo->SetStringField(TEXT("type"), VarTypeToSimpleString(Vd.VarType));
		VarArr.Add(MakeShareable(new FJsonValueObject(Vo.ToSharedRef())));
	}
	Root->SetArrayField(TEXT("variables"), VarArr);

	TMap<UEdGraphNode*, FString> IdByNode;
	TArray<TSharedPtr<FJsonValue>> NodeArr;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N)
		{
			continue;
		}
		const FString Nid = NodeIdString(N);
		IdByNode.Add(N, Nid);
		FString Op;
		TSharedPtr<FJsonObject> No;
		ExportNodeFields(N, No, Op);
		No->SetStringField(TEXT("node_id"), Nid);
		NodeArr.Add(MakeShareable(new FJsonValueObject(No.ToSharedRef())));
	}
	Root->SetArrayField(TEXT("nodes"), NodeArr);

	TArray<TSharedPtr<FJsonValue>> LinkArr;
	TArray<TSharedPtr<FJsonValue>> DefArr;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N)
		{
			continue;
		}
		const FString* FromIdPtr = IdByNode.Find(N);
		if (!FromIdPtr)
		{
			continue;
		}
		const FString& FromId = *FromIdPtr;
		for (UEdGraphPin* P : N->Pins)
		{
			if (!P)
			{
				continue;
			}
			if (P->Direction == EGPD_Output)
			{
				for (UEdGraphPin* LP : P->LinkedTo)
				{
					if (!LP || !LP->GetOwningNode())
					{
						continue;
					}
					const FString* ToIdPtr = IdByNode.Find(LP->GetOwningNode());
					if (!ToIdPtr)
					{
						continue;
					}
					const FString& ToId = *ToIdPtr;
					TSharedPtr<FJsonObject> Lo = MakeShared<FJsonObject>();
					Lo->SetStringField(
						TEXT("from"),
						FString::Printf(TEXT("%s.%s"), *FromId, *P->PinName.ToString()));
					Lo->SetStringField(
						TEXT("to"),
						FString::Printf(TEXT("%s.%s"), *ToId, *LP->PinName.ToString()));
					LinkArr.Add(MakeShareable(new FJsonValueObject(Lo.ToSharedRef())));
				}
			}
			if (P->Direction == EGPD_Input && P->LinkedTo.Num() == 0 && !P->DefaultValue.IsEmpty())
			{
				TSharedPtr<FJsonObject> Do = MakeShared<FJsonObject>();
				Do->SetStringField(TEXT("node_id"), FromId);
				Do->SetStringField(TEXT("pin"), P->PinName.ToString());
				Do->SetStringField(TEXT("value"), P->DefaultValue);
				DefArr.Add(MakeShareable(new FJsonValueObject(Do.ToSharedRef())));
			}
		}
	}
	Root->SetArrayField(TEXT("links"), LinkArr);
	Root->SetArrayField(TEXT("defaults"), DefArr);

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"), true);
	Out->SetObjectField(TEXT("ir"), Root);
	return UnrealAiToolJson::Ok(Out);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintApplyIr(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiBlueprintToolsPriv;

	FBlueprintIr Ir;
	TArray<FIrError> ParseErrors;
	if (!TryParseIr(Args, Ir, ParseErrors))
	{
		return MakeIrErrorResult(TEXT("invalid_ir"), TEXT("Invalid blueprint IR"), ParseErrors);
	}

	UBlueprint* BP = LoadBlueprint(Ir.BlueprintPath);
	if (!BP)
	{
		TArray<FIrError> Errors;
		AddError(
			Errors,
			TEXT("asset_not_found"),
			TEXT("$.blueprint_path"),
			TEXT("Could not load Blueprint"),
			TEXT("Create the Blueprint first and provide full object path"));
		return MakeIrErrorResult(TEXT("asset_not_found"), TEXT("Blueprint load failed"), Errors);
	}

	UEdGraph* Graph = FindGraphByName(BP, Ir.GraphName);
	if (!Graph)
	{
		TArray<FIrError> Errors;
		AddError(
			Errors,
			TEXT("graph_not_found"),
			TEXT("$.graph_name"),
			TEXT("Graph not found on Blueprint"),
			TEXT("For v1 use EventGraph (default)"));
		return MakeIrErrorResult(TEXT("graph_not_found"), TEXT("Graph resolution failed"), Errors);
	}

	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnBpApplyIr", "Unreal AI: apply blueprint IR"));
	BP->Modify();
	Graph->Modify();

	for (const FIrVariableDecl& Var : Ir.Variables)
	{
		if (FBlueprintEditorUtils::FindNewVariableIndex(BP, FName(*Var.Name)) != INDEX_NONE)
		{
			continue;
		}
		FBlueprintEditorUtils::AddMemberVariable(BP, FName(*Var.Name), ParsePinType(Var.Type));
	}

	TMap<FString, UEdGraphNode*> NodeById;
	TArray<FIrError> BuildErrors;
	for (const FIrNodeDecl& D : Ir.Nodes)
	{
		if (NodeById.Contains(D.NodeId))
		{
			AddError(
				BuildErrors,
				TEXT("duplicate_node_id"),
				FString::Printf(TEXT("$.nodes[%s]"), *D.NodeId),
				TEXT("node_id must be unique"),
				TEXT(""));
			continue;
		}
		if (UEdGraphNode* N = CreateNodeFromDecl(BP, Graph, D, BuildErrors))
		{
			NodeById.Add(D.NodeId, N);
		}
	}

	for (const FIrDefaultDecl& D : Ir.Defaults)
	{
		UEdGraphNode* Node = NodeById.FindRef(D.NodeId);
		if (!Node)
		{
			AddError(
				BuildErrors,
				TEXT("unknown_node"),
				FString::Printf(TEXT("$.defaults[%s]"), *D.NodeId),
				TEXT("defaults references unknown node_id"),
				TEXT(""));
			continue;
		}
		UEdGraphPin* Pin = FindPinByName(Node, D.Pin);
		if (!Pin)
		{
			AddError(
				BuildErrors,
				TEXT("unknown_pin"),
				FString::Printf(TEXT("$.defaults[%s].pin"), *D.NodeId),
				TEXT("Pin not found for default assignment"),
				TEXT("Use exact pin name from blueprint_get_graph_summary + editor inspection"));
			continue;
		}
		Pin->DefaultValue = D.Value;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	for (const FIrLinkDecl& L : Ir.Links)
	{
		FString FromNodeId;
		FString FromPinName;
		FString ToNodeId;
		FString ToPinName;
		if (!SplitNodePin(L.From, FromNodeId, FromPinName) || !SplitNodePin(L.To, ToNodeId, ToPinName))
		{
			AddError(
				BuildErrors,
				TEXT("invalid_link"),
				TEXT("$.links"),
				TEXT("Link endpoints must be node_id.pin"),
				TEXT("Example: begin_play.Then -> branch.Execute"));
			continue;
		}
		UEdGraphNode* FromNode = NodeById.FindRef(FromNodeId);
		UEdGraphNode* ToNode = NodeById.FindRef(ToNodeId);
		if (!FromNode || !ToNode)
		{
			AddError(
				BuildErrors,
				TEXT("unknown_node"),
				TEXT("$.links"),
				TEXT("Link references unknown node_id"),
				TEXT(""));
			continue;
		}
		UEdGraphPin* A = FindPinByName(FromNode, FromPinName);
		UEdGraphPin* B = FindPinByName(ToNode, ToPinName);
		if (!A || !B)
		{
			AddError(
				BuildErrors,
				TEXT("unknown_pin"),
				TEXT("$.links"),
				TEXT("Link references unknown pin"),
				TEXT("Check pin names on both nodes"));
			continue;
		}
		if (!Schema->TryCreateConnection(A, B))
		{
			AddError(
				BuildErrors,
				TEXT("type_mismatch"),
				TEXT("$.links"),
				TEXT("Could not connect pins"),
				TEXT("Check pin directions and types"));
		}
	}

	if (BuildErrors.Num() > 0)
	{
		return MakeIrErrorResult(TEXT("apply_failed"), TEXT("Blueprint IR apply failed"), BuildErrors);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), BP->Status != BS_Error);
	O->SetStringField(TEXT("blueprint_path"), Ir.BlueprintPath);
	O->SetStringField(TEXT("graph_name"), Ir.GraphName);
	O->SetNumberField(TEXT("node_count"), static_cast<double>(NodeById.Num()));
	O->SetNumberField(TEXT("link_count"), static_cast<double>(Ir.Links.Num()));
	O->SetNumberField(TEXT("blueprint_status"), static_cast<double>(static_cast<int32>(BP->Status)));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required"));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint"));
	}
	TArray<UEdGraph*> Graphs;
	if (!GraphName.IsEmpty())
	{
		if (UEdGraph* G = UnrealAiBlueprintToolsPriv::FindGraphByName(BP, GraphName))
		{
			Graphs.Add(G);
		}
	}
	else
	{
		Graphs.Append(BP->UbergraphPages);
		Graphs.Append(BP->FunctionGraphs);
		Graphs.Append(BP->MacroGraphs);
	}
	TArray<TSharedPtr<FJsonValue>> GraphArr;
	for (UEdGraph* G : Graphs)
	{
		if (!G)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Go = MakeShared<FJsonObject>();
		Go->SetStringField(TEXT("name"), G->GetName());
		int32 Nodes = 0;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (N)
			{
				++Nodes;
			}
		}
		Go->SetNumberField(TEXT("node_count"), static_cast<double>(Nodes));
		GraphArr.Add(MakeShareable(new FJsonValueObject(Go.ToSharedRef())));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetArrayField(TEXT("graphs"), GraphArr);
	O->SetNumberField(TEXT("graph_count"), static_cast<double>(GraphArr.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintOpenGraphTab(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	FString GraphName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty()
		|| !Args->TryGetStringField(TEXT("graph_name"), GraphName) || GraphName.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path and graph_name are required"));
	}
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint"));
	}
	UEdGraph* G = UnrealAiBlueprintToolsPriv::FindGraphByName(BP, GraphName);
	if (!G)
	{
		return UnrealAiToolJson::Error(TEXT("Graph not found"));
	}
	TSharedPtr<FJsonObject> OpenArgs = MakeShared<FJsonObject>();
	OpenArgs->SetStringField(TEXT("object_path"), Path);
	const FUnrealAiToolInvocationResult OpenRes = UnrealAiDispatch_AssetOpenEditor(OpenArgs);
	if (!OpenRes.bOk)
	{
		return OpenRes;
	}
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(G);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("graph"), GraphName);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintAddVariable(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	FString VarName;
	FString TypeStr;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty()
		|| !Args->TryGetStringField(TEXT("name"), VarName) || VarName.IsEmpty()
		|| !Args->TryGetStringField(TEXT("type"), TypeStr) || TypeStr.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path, name, and type are required"));
	}
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint"));
	}
	const FEdGraphPinType PinType = UnrealAiBlueprintToolsPriv::ParsePinType(TypeStr);
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnBpVar", "Unreal AI: add BP variable"));
	FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AnimBlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("anim_blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("anim_blueprint_path is required"));
	}
	if (!LoadObject<UAnimBlueprint>(nullptr, *Path))
	{
		return UnrealAiToolJson::Error(TEXT("Could not load AnimBlueprint"));
	}
	TSharedPtr<FJsonObject> Fake = MakeShared<FJsonObject>();
	Fake->SetStringField(TEXT("blueprint_path"), Path);
	return UnrealAiDispatch_BlueprintGetGraphSummary(Fake);
}
