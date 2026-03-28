#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

#include "Tools/UnrealAiToolDispatch_ArgRepair.h"
#include "Tools/UnrealAiBlueprintFormatterBridge.h"
#include "Tools/UnrealAiBlueprintIrHallucinationNormalizer.h"
#include "Tools/UnrealAiToolDispatch_MoreAssets.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/Presentation/UnrealAiToolEditorNoteBuilders.h"

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
#include "Subsystems/AssetEditorSubsystem.h"
#include "ScopedTransaction.h"
#include "Misc/PackageName.h"
#include "Tools/Presentation/UnrealAiEditorNavigation.h"
#include "UObject/Class.h"
#include "UObject/Field.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "AssetRegistry/AssetRegistryModule.h"

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
		bool bCreateIfMissing = false;
		FString ParentClassPath;
		/** When true, run formatter after apply if all IR node positions are zero (requires UnrealBlueprintFormatter). */
		bool bAutoLayout = true;
		/** create_new | append_to_existing — empty uses default (append on ubergraph, create_new on function/macro graphs). */
		FString MergePolicy;
		/** ir_nodes | full_graph — ir_nodes layouts only IR-touched nodes; full_graph runs LayoutEntireGraph on the target graph. */
		FString LayoutScope;
	};

	struct FEventMergePlan
	{
		bool bMerged = false;
		UK2Node_Event* ChosenEvent = nullptr;
		UEdGraphNode* TailNode = nullptr;
		FName MemberName = NAME_None;
		int32 DuplicateAnchors = 0;
	};

	struct FIrNormalizationReport
	{
		bool bApplied = false;
		TArray<FString> Notes;
		TArray<FString> DeprecatedFieldsSeen;
	};

	static FString NormalizeIrOpToken(const FString& InOp, FIrNormalizationReport& Report)
	{
		const FString Op = InOp.ToLower();
		if (Op == TEXT("event_begin_overlap") || Op == TEXT("event_actor_begin_overlap")
			|| Op == TEXT("begin_overlap")
			|| Op == TEXT("on_actor_begin_overlap") || Op == TEXT("actor_begin_overlap"))
		{
			Report.bApplied = true;
			Report.DeprecatedFieldsSeen.Add(TEXT("op:event_begin_overlap_alias"));
			Report.Notes.Add(TEXT("Mapped overlap event op alias -> event_actor_begin_overlap."));
			return TEXT("event_actor_begin_overlap");
		}
		if (Op == TEXT("add_movement_input"))
		{
			Report.bApplied = true;
			Report.DeprecatedFieldsSeen.Add(TEXT("op:add_movement_input"));
			Report.Notes.Add(TEXT("Mapped deprecated op add_movement_input -> call_function(/Script/Engine.Pawn.AddMovementInput)."));
			return TEXT("call_function");
		}
		if (Op == TEXT("turn_at_rate"))
		{
			Report.bApplied = true;
			Report.DeprecatedFieldsSeen.Add(TEXT("op:turn_at_rate"));
			Report.Notes.Add(TEXT("Mapped deprecated op turn_at_rate -> call_function(/Script/Engine.Pawn.AddControllerYawInput)."));
			return TEXT("call_function");
		}
		if (Op == TEXT("lookup_at_rate"))
		{
			Report.bApplied = true;
			Report.DeprecatedFieldsSeen.Add(TEXT("op:lookup_at_rate"));
			Report.Notes.Add(TEXT("Mapped deprecated op lookup_at_rate -> call_function(/Script/Engine.Pawn.AddControllerPitchInput)."));
			return TEXT("call_function");
		}
		return InOp;
	}

	static void NormalizeBlueprintIrArgs(const TSharedPtr<FJsonObject>& Args, FIrNormalizationReport& Report)
	{
		if (!Args.IsValid())
		{
			return;
		}
		FString Path;
		if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
		{
			Args->TryGetStringField(TEXT("object_path"), Path);
			if (Path.IsEmpty())
			{
				Args->TryGetStringField(TEXT("asset_path"), Path);
			}
			if (!Path.IsEmpty())
			{
				Args->SetStringField(TEXT("blueprint_path"), Path);
				Report.bApplied = true;
				Report.DeprecatedFieldsSeen.Add(TEXT("blueprint_path_alias"));
			}
		}
		FString GraphName;
		if (!Args->TryGetStringField(TEXT("graph_name"), GraphName) || GraphName.IsEmpty())
		{
			Args->TryGetStringField(TEXT("graph"), GraphName);
			if (!GraphName.IsEmpty())
			{
				Args->SetStringField(TEXT("graph_name"), GraphName);
				Report.bApplied = true;
				Report.DeprecatedFieldsSeen.Add(TEXT("graph_name_alias"));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		TSet<FString> ReferencedVars;
		TMap<FString, FString> NodeOpById;
		if (Args->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes)
		{
			for (const TSharedPtr<FJsonValue>& V : *Nodes)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
				{
					continue;
				}
				TSharedPtr<FJsonObject> Node = *O;
				// Node key alias repair (node_id / variable / class / function).
				// The downstream strict parser requires these canonical keys.
				{
					FString CanonicalNodeId;
					if (!Node->TryGetStringField(TEXT("node_id"), CanonicalNodeId) || CanonicalNodeId.IsEmpty())
					{
						FString AltNodeId;
						if (Node->TryGetStringField(TEXT("nodeId"), AltNodeId) && !AltNodeId.IsEmpty())
						{
							Node->SetStringField(TEXT("node_id"), AltNodeId);
							Report.bApplied = true;
							Report.DeprecatedFieldsSeen.Add(TEXT("nodes.nodeId"));
							Report.Notes.Add(TEXT("Aliased nodes[i].nodeId -> nodes[i].node_id"));
						}
						else if (Node->TryGetStringField(TEXT("id"), AltNodeId) && !AltNodeId.IsEmpty())
						{
							Node->SetStringField(TEXT("node_id"), AltNodeId);
							Report.bApplied = true;
							Report.DeprecatedFieldsSeen.Add(TEXT("nodes.id"));
							Report.Notes.Add(TEXT("Aliased nodes[i].id -> nodes[i].node_id"));
						}
					}
				}

				FString Op;
				Node->TryGetStringField(TEXT("op"), Op);
				{
					// Centralized hallucination map for unsupported pseudo-ops.
					if (UnrealAiBlueprintIrHallucinationNormalizer::NormalizeNode(
						Node,
						Report.Notes,
						Report.DeprecatedFieldsSeen))
					{
						Report.bApplied = true;
						Node->TryGetStringField(TEXT("op"), Op);
					}
				}
				{
					// Hallucinated event shape: op=K2Node_Event + title contains overlap event.
					if (Op.Equals(TEXT("K2Node_Event"), ESearchCase::IgnoreCase))
					{
						FString Title;
						Node->TryGetStringField(TEXT("title"), Title);
						if (Title.Contains(TEXT("ActorBeginOverlap"), ESearchCase::IgnoreCase)
							|| Title.Contains(TEXT("BeginOverlap"), ESearchCase::IgnoreCase))
						{
							Node->SetStringField(TEXT("op"), TEXT("event_actor_begin_overlap"));
							Report.bApplied = true;
							Report.DeprecatedFieldsSeen.Add(TEXT("nodes.op.K2Node_Event.ActorBeginOverlap"));
							Report.Notes.Add(TEXT("Mapped K2Node_Event ActorBeginOverlap -> event_actor_begin_overlap"));
							Op = TEXT("event_actor_begin_overlap");
						}
					}
				}
				if (!Op.IsEmpty())
				{
					const FString CanonOp = NormalizeIrOpToken(Op, Report);
					if (!CanonOp.Equals(Op, ESearchCase::CaseSensitive))
					{
						Node->SetStringField(TEXT("op"), CanonOp);
						Op = CanonOp;
					}
					if (CanonOp == TEXT("call_function"))
					{
						// If class_path points to a Blueprint asset path (/Game/...), we cannot
						// resolve a UFunction symbol directly. Convert to custom_event so the IR
						// still applies and the agent can continue instead of looping on symbol lookup.
						{
							FString CP;
							if (Node->TryGetStringField(TEXT("class_path"), CP)
								&& CP.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase))
							{
								Node->SetStringField(TEXT("op"), TEXT("custom_event"));
								Report.bApplied = true;
								Report.DeprecatedFieldsSeen.Add(TEXT("call_function.blueprint_class_path_fallback"));
								Report.Notes.Add(TEXT("Mapped call_function with /Game class_path to custom_event fallback."));
							}
						}
						// Common hallucination: class_path=/Script/Engine.Log function_name=Log
						// In Blueprints, "logging" is typically UKismetSystemLibrary::PrintString (or PrintText).
						{
							FString CP;
							FString FN;
							Node->TryGetStringField(TEXT("class_path"), CP);
							Node->TryGetStringField(TEXT("function_name"), FN);
							if (CP.Equals(TEXT("/Script/Engine.Log"), ESearchCase::IgnoreCase)
								&& FN.Equals(TEXT("Log"), ESearchCase::IgnoreCase))
							{
								Node->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.KismetSystemLibrary"));
								Node->SetStringField(TEXT("function_name"), TEXT("PrintString"));
								Report.bApplied = true;
								Report.DeprecatedFieldsSeen.Add(TEXT("call_function.Engine.Log.Log"));
								Report.Notes.Add(TEXT("Mapped call_function /Script/Engine.Log::Log -> /Script/Engine.KismetSystemLibrary::PrintString"));
							}
						}
						if (Op.Equals(TEXT("add_movement_input"), ESearchCase::IgnoreCase))
						{
							Node->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.Pawn"));
							Node->SetStringField(TEXT("function_name"), TEXT("AddMovementInput"));
						}
						else if (Op.Equals(TEXT("turn_at_rate"), ESearchCase::IgnoreCase))
						{
							Node->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.Pawn"));
							Node->SetStringField(TEXT("function_name"), TEXT("AddControllerYawInput"));
						}
						else if (Op.Equals(TEXT("lookup_at_rate"), ESearchCase::IgnoreCase))
						{
							Node->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.Pawn"));
							Node->SetStringField(TEXT("function_name"), TEXT("AddControllerPitchInput"));
						}
					}
				}
				{
					FString NodeId;
					if (Node->TryGetStringField(TEXT("node_id"), NodeId) && !NodeId.IsEmpty() && !Op.IsEmpty())
					{
						NodeOpById.Add(NodeId, Op.ToLower());
					}
				}
				FString Alias;
				if (!Node->HasField(TEXT("variable")) && Node->TryGetStringField(TEXT("variable_name"), Alias) && !Alias.IsEmpty())
				{
					Node->SetStringField(TEXT("variable"), Alias);
					Report.bApplied = true;
					Report.DeprecatedFieldsSeen.Add(TEXT("variable_name"));
				}
				if (!Node->HasField(TEXT("variable")) && Node->HasTypedField<EJson::Object>(TEXT("vars")))
				{
					const TSharedPtr<FJsonObject> VarsObj = Node->GetObjectField(TEXT("vars"));
					FString VarsName;
					if (VarsObj.IsValid() && VarsObj->TryGetStringField(TEXT("name"), VarsName) && !VarsName.IsEmpty())
					{
						Node->SetStringField(TEXT("variable"), VarsName);
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("vars.name"));
						Report.Notes.Add(TEXT("Aliased nodes[i].vars.name -> nodes[i].variable"));
					}
				}
				if (!Node->HasField(TEXT("class_path")) && Node->TryGetStringField(TEXT("class"), Alias) && !Alias.IsEmpty())
				{
					Node->SetStringField(TEXT("class_path"), Alias);
					Report.bApplied = true;
					Report.DeprecatedFieldsSeen.Add(TEXT("class"));
				}
				if (!Node->HasField(TEXT("function_name")) && Node->TryGetStringField(TEXT("function"), Alias) && !Alias.IsEmpty())
				{
					Node->SetStringField(TEXT("function_name"), Alias);
					Report.bApplied = true;
					Report.DeprecatedFieldsSeen.Add(TEXT("function"));
				}
				// Common fallback: some model outputs put callable name in `name`
				// for call_function nodes instead of `function_name`.
				{
					FString NodeOp;
					if (!Node->HasField(TEXT("function_name"))
						&& Node->TryGetStringField(TEXT("name"), Alias) && !Alias.IsEmpty()
						&& Node->TryGetStringField(TEXT("op"), NodeOp)
						&& NodeOp.Equals(TEXT("call_function"), ESearchCase::IgnoreCase))
					{
						Node->SetStringField(TEXT("function_name"), Alias);
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("name_for_call_function"));
						Report.Notes.Add(TEXT("Aliased call_function node name -> function_name"));
					}
				}

				// `custom_event` nodes require `name` (custom function name) for K2 pin/signature creation.
				// Models often emit only `node_id` + `op:custom_event`; treat `node_id` (or `function_name`)
				// as the implicit custom event name to reduce avoidable apply failures.
				{
					FString NodeOp;
					if (Node->TryGetStringField(TEXT("op"), NodeOp) && NodeOp.Equals(TEXT("custom_event"), ESearchCase::IgnoreCase))
					{
						FString ExistingName;
						const bool bHasName = Node->TryGetStringField(TEXT("name"), ExistingName) && !ExistingName.IsEmpty();
						if (!bHasName)
						{
							FString ImplicitName;
							if (Node->TryGetStringField(TEXT("function_name"), ImplicitName) && !ImplicitName.IsEmpty())
							{
								Node->SetStringField(TEXT("name"), ImplicitName);
								Report.bApplied = true;
								Report.DeprecatedFieldsSeen.Add(TEXT("function_name_for_custom_event_name"));
								Report.Notes.Add(TEXT("custom_event.name missing; mapped from nodes.function_name"));
							}
							else if (Node->TryGetStringField(TEXT("node_id"), ImplicitName) && !ImplicitName.IsEmpty())
							{
								Node->SetStringField(TEXT("name"), ImplicitName);
								Report.bApplied = true;
								Report.DeprecatedFieldsSeen.Add(TEXT("node_id_for_custom_event_name"));
								Report.Notes.Add(TEXT("custom_event.name missing; mapped from nodes.node_id"));
							}
						}
					}
				}

				// Best-effort: if nodes reference variables via get_variable/set_variable without declaring them,
				// synthesize variable declarations so apply doesn't fail on missing vars.
				{
					FString NodeOp;
					if (Node->TryGetStringField(TEXT("op"), NodeOp)
						&& (NodeOp.Equals(TEXT("get_variable"), ESearchCase::IgnoreCase)
							|| NodeOp.Equals(TEXT("set_variable"), ESearchCase::IgnoreCase)))
					{
						FString Var;
						if (Node->TryGetStringField(TEXT("variable"), Var) && !Var.IsEmpty())
						{
							ReferencedVars.Add(Var);
						}
					}
				}
			}
		}

		// Add implicit variables[] entries for referenced vars not explicitly declared.
		if (ReferencedVars.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> VarsArr;
			TSet<FString> ExistingVarNames;
			const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
			if (Args->TryGetArrayField(TEXT("variables"), Variables) && Variables)
			{
				VarsArr = *Variables;
				for (const TSharedPtr<FJsonValue>& VV : *Variables)
				{
					const TSharedPtr<FJsonObject>* VO = nullptr;
					if (VV.IsValid() && VV->TryGetObject(VO) && VO && (*VO).IsValid())
					{
						FString N;
						if ((*VO)->TryGetStringField(TEXT("name"), N) && !N.IsEmpty())
						{
							ExistingVarNames.Add(N);
						}
					}
				}
			}

			int32 Added = 0;
			for (const FString& VarName : ReferencedVars)
			{
				if (ExistingVarNames.Contains(VarName))
				{
					continue;
				}
				TSharedPtr<FJsonObject> VD = MakeShared<FJsonObject>();
				VD->SetStringField(TEXT("name"), VarName);

				// Heuristic: bXxx -> bool, otherwise float.
				const bool bBoolStyle = VarName.Len() >= 2 && VarName[0] == TCHAR('b') && FChar::IsUpper(VarName[1]);
				VD->SetStringField(TEXT("type"), bBoolStyle ? TEXT("bool") : TEXT("float"));

				VarsArr.Add(MakeShareable(new FJsonValueObject(VD.ToSharedRef())));
				ExistingVarNames.Add(VarName);
				++Added;
			}

			if (Added > 0)
			{
				Args->SetArrayField(TEXT("variables"), VarsArr);
				Report.bApplied = true;
				Report.DeprecatedFieldsSeen.Add(TEXT("implicit_variables"));
				Report.Notes.Add(FString::Printf(TEXT("Added %d implicit variables[] entries for referenced get/set_variable nodes"), Added));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
		if (Args->TryGetArrayField(TEXT("links"), Links) && Links)
		{
			for (const TSharedPtr<FJsonValue>& V : *Links)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
				{
					continue;
				}
				TSharedPtr<FJsonObject> Link = *O;
				FString From;
				FString To;
				if (!Link->TryGetStringField(TEXT("from"), From) || From.IsEmpty())
				{
					if (Link->TryGetStringField(TEXT("source"), From) && !From.IsEmpty())
					{
						Link->SetStringField(TEXT("from"), From);
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("links.source"));
					}
				}
				if (!Link->TryGetStringField(TEXT("to"), To) || To.IsEmpty())
				{
					if (Link->TryGetStringField(TEXT("target"), To) && !To.IsEmpty())
					{
						Link->SetStringField(TEXT("to"), To);
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("links.target"));
					}
				}
				auto EndpointNodeId = [](const FString& Endpoint) -> FString
				{
					int32 Dot = INDEX_NONE;
					if (Endpoint.FindLastChar(TEXT('.'), Dot) && Dot > 0)
					{
						return Endpoint.Left(Dot);
					}
					return Endpoint;
				};
				const FString FromNodeIdRaw = EndpointNodeId(From);
				const FString ToNodeIdRaw = EndpointNodeId(To);
				const FString* FromOpPtr = NodeOpById.Find(FromNodeIdRaw);
				const FString* ToOpPtr = NodeOpById.Find(ToNodeIdRaw);
				const bool bFromIsEvent = FromOpPtr && FromOpPtr->StartsWith(TEXT("event_"));
				const bool bToIsEvent = ToOpPtr && ToOpPtr->StartsWith(TEXT("event_"));
				if (!From.IsEmpty() && !To.IsEmpty() && !bFromIsEvent && bToIsEvent)
				{
					Swap(From, To);
					Link->SetStringField(TEXT("from"), From);
					Link->SetStringField(TEXT("to"), To);
					Report.bApplied = true;
					Report.DeprecatedFieldsSeen.Add(TEXT("links.reversed_event_direction"));
					Report.Notes.Add(TEXT("Reversed link direction when target was an event node."));
				}

				if (!From.IsEmpty() && !From.Contains(TEXT(".")))
				{
					Link->SetStringField(TEXT("from"), From + TEXT(".Then"));
					Report.bApplied = true;
					Report.Notes.Add(TEXT("Normalized link.from without pin to .Then"));
				}
				else if (!From.IsEmpty() && From.Contains(TEXT(".")))
				{
					int32 Dot = INDEX_NONE;
					From.FindLastChar(TEXT('.'), Dot);
					if (Dot > 0 && Dot < From.Len() - 1)
					{
						const FString NodeId = From.Left(Dot);
						const FString PinTok = From.Mid(Dot + 1);
						FString CanonPin = PinTok;
						const FString* FromNodeOp = NodeOpById.Find(NodeId);
						if (PinTok.Equals(TEXT("Exec"), ESearchCase::IgnoreCase) || PinTok.Equals(TEXT("Execute"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Execute.ToString();
						}
						else if (PinTok.Equals(TEXT("Trigger"), ESearchCase::IgnoreCase)
							|| PinTok.Equals(TEXT("OnActorBeginOverlap"), ESearchCase::IgnoreCase)
							|| PinTok.Equals(TEXT("Finished"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Then.ToString();
						}
						else if ((PinTok.Equals(TEXT("Overlapped"), ESearchCase::IgnoreCase)
								|| PinTok.Equals(TEXT("OverlappedActor"), ESearchCase::IgnoreCase)
								|| PinTok.Equals(TEXT("OtherActor"), ESearchCase::IgnoreCase))
							&& FromNodeOp && FromNodeOp->StartsWith(TEXT("event_")))
						{
							CanonPin = UEdGraphSchema_K2::PN_Then.ToString();
						}
						else if (PinTok.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Then.ToString();
						}
						else if (PinTok.Equals(TEXT("Then"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Then.ToString();
						}
						else if (PinTok.Equals(TEXT("Else"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Else.ToString();
						}
						else if (PinTok.Equals(TEXT("Condition"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Condition.ToString();
						}
						if (!CanonPin.Equals(PinTok, ESearchCase::CaseSensitive))
						{
							Link->SetStringField(TEXT("from"), NodeId + TEXT(".") + CanonPin);
							Report.bApplied = true;
							Report.DeprecatedFieldsSeen.Add(TEXT("links.pin_token_alias.from"));
							Report.Notes.Add(TEXT("Normalized link.from pin token alias to canonical K2 pin name"));
						}
					}
				}
				if (!To.IsEmpty() && !To.Contains(TEXT(".")))
				{
					Link->SetStringField(TEXT("to"), To + TEXT(".Execute"));
					Report.bApplied = true;
					Report.Notes.Add(TEXT("Normalized link.to without pin to .Execute"));
				}
				else if (!To.IsEmpty() && To.Contains(TEXT(".")))
				{
					int32 Dot = INDEX_NONE;
					To.FindLastChar(TEXT('.'), Dot);
					if (Dot > 0 && Dot < To.Len() - 1)
					{
						const FString NodeId = To.Left(Dot);
						const FString PinTok = To.Mid(Dot + 1);
						FString CanonPin = PinTok;
						if (PinTok.Equals(TEXT("Exec"), ESearchCase::IgnoreCase) || PinTok.Equals(TEXT("Execute"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Execute.ToString();
						}
						else if (PinTok.Equals(TEXT("Trigger"), ESearchCase::IgnoreCase)
							|| PinTok.Equals(TEXT("OnActorBeginOverlap"), ESearchCase::IgnoreCase)
							|| PinTok.Equals(TEXT("Finished"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Execute.ToString();
						}
						else if (PinTok.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Then.ToString();
						}
						else if (PinTok.Equals(TEXT("Then"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Then.ToString();
						}
						else if (PinTok.Equals(TEXT("Else"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Else.ToString();
						}
						else if (PinTok.Equals(TEXT("Condition"), ESearchCase::IgnoreCase))
						{
							CanonPin = UEdGraphSchema_K2::PN_Condition.ToString();
						}
						if (!CanonPin.Equals(PinTok, ESearchCase::CaseSensitive))
						{
							Link->SetStringField(TEXT("to"), NodeId + TEXT(".") + CanonPin);
							Report.bApplied = true;
							Report.DeprecatedFieldsSeen.Add(TEXT("links.pin_token_alias.to"));
							Report.Notes.Add(TEXT("Normalized link.to pin token alias to canonical K2 pin name"));
						}
					}
				}
			}
		}

		// Defaults repair (optional in schema, but common in AI-generated IR).
		const TArray<TSharedPtr<FJsonValue>>* Defaults = nullptr;
		if (Args->TryGetArrayField(TEXT("defaults"), Defaults) && Defaults)
		{
			TArray<TSharedPtr<FJsonValue>> RewrittenDefaults;
			RewrittenDefaults.Reserve(Defaults->Num());
			bool bRewroteDefaults = false;

			TArray<TSharedPtr<FJsonValue>> VariablesArr;
			const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
			if (Args->TryGetArrayField(TEXT("variables"), Variables) && Variables)
			{
				VariablesArr = *Variables;
			}

			for (const TSharedPtr<FJsonValue>& V : *Defaults)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
				{
					continue;
				}
				TSharedPtr<FJsonObject> Default = *O;
				// Some models put variable declarations in defaults[] as:
				// { name, type, default }. Rewrite these into variables[] and drop
				// from defaults[] so strict IR parsing does not fail.
				{
					FString DeclName;
					FString DeclType;
					FString ExistingNodeId;
					const bool bHasNodeId =
						Default->TryGetStringField(TEXT("node_id"), ExistingNodeId) && !ExistingNodeId.IsEmpty();
					if (!bHasNodeId
						&& Default->TryGetStringField(TEXT("name"), DeclName) && !DeclName.IsEmpty()
						&& Default->TryGetStringField(TEXT("type"), DeclType) && !DeclType.IsEmpty())
					{
						FString CanonType = DeclType.ToLower();
						if (CanonType == TEXT("boolean"))
						{
							CanonType = TEXT("bool");
						}
						else if (CanonType == TEXT("integer"))
						{
							CanonType = TEXT("int");
						}
						TSharedPtr<FJsonObject> VarDecl = MakeShared<FJsonObject>();
						VarDecl->SetStringField(TEXT("name"), DeclName);
						VarDecl->SetStringField(TEXT("type"), CanonType);
						bool BoolDefault = false;
						double NumberDefault = 0.0;
						FString StringDefault;
						if (Default->TryGetBoolField(TEXT("default"), BoolDefault))
						{
							VarDecl->SetBoolField(TEXT("default"), BoolDefault);
						}
						else if (Default->TryGetNumberField(TEXT("default"), NumberDefault))
						{
							VarDecl->SetNumberField(TEXT("default"), NumberDefault);
						}
						else if (Default->TryGetStringField(TEXT("default"), StringDefault))
						{
							VarDecl->SetStringField(TEXT("default"), StringDefault);
						}
						VariablesArr.Add(MakeShared<FJsonValueObject>(VarDecl));
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("defaults.variable_decl_shape"));
						Report.Notes.Add(TEXT("Rewrote defaults[i] {name,type,default} into variables[] declaration."));
						bRewroteDefaults = true;
						continue;
					}
				}
				FString NodeId;
				if (!Default->TryGetStringField(TEXT("node_id"), NodeId) || NodeId.IsEmpty())
				{
					FString Alt;
					if (Default->TryGetStringField(TEXT("nodeId"), Alt) && !Alt.IsEmpty())
					{
						Default->SetStringField(TEXT("node_id"), Alt);
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("defaults.nodeId"));
						Report.Notes.Add(TEXT("Aliased defaults[i].nodeId -> defaults[i].node_id"));
					}
					else if (Default->TryGetStringField(TEXT("id"), Alt) && !Alt.IsEmpty())
					{
						Default->SetStringField(TEXT("node_id"), Alt);
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("defaults.id"));
						Report.Notes.Add(TEXT("Aliased defaults[i].id -> defaults[i].node_id"));
					}
				}
				FString Pin;
				if (Default->TryGetStringField(TEXT("pin"), Pin) && !Pin.IsEmpty())
				{
					const FString PinTok = Pin;
					if (PinTok.Equals(TEXT("Exec"), ESearchCase::IgnoreCase) || PinTok.Equals(TEXT("Execute"), ESearchCase::IgnoreCase))
					{
						Default->SetStringField(TEXT("pin"), UEdGraphSchema_K2::PN_Execute.ToString());
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("defaults.pin_token.Exec"));
						Report.Notes.Add(TEXT("Normalized defaults[i].pin Exec/Execute -> execute"));
					}
					else if (PinTok.Equals(TEXT("Then"), ESearchCase::IgnoreCase))
					{
						Default->SetStringField(TEXT("pin"), UEdGraphSchema_K2::PN_Then.ToString());
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("defaults.pin_token.Then"));
						Report.Notes.Add(TEXT("Normalized defaults[i].pin Then -> then"));
					}
					else if (PinTok.Equals(TEXT("Else"), ESearchCase::IgnoreCase))
					{
						Default->SetStringField(TEXT("pin"), UEdGraphSchema_K2::PN_Else.ToString());
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("defaults.pin_token.Else"));
						Report.Notes.Add(TEXT("Normalized defaults[i].pin Else -> else"));
					}
					else if (PinTok.Equals(TEXT("Condition"), ESearchCase::IgnoreCase))
					{
						Default->SetStringField(TEXT("pin"), UEdGraphSchema_K2::PN_Condition.ToString());
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("defaults.pin_token.Condition"));
						Report.Notes.Add(TEXT("Normalized defaults[i].pin Condition -> condition"));
					}
				}
				else
				{
					FString AltPin;
					if (Default->TryGetStringField(TEXT("pin_name"), AltPin) && !AltPin.IsEmpty())
					{
						Default->SetStringField(TEXT("pin"), AltPin);
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("defaults.pin_name"));
						Report.Notes.Add(TEXT("Aliased defaults[i].pin_name -> defaults[i].pin"));
					}
					else if (Default->TryGetStringField(TEXT("pinToken"), AltPin) && !AltPin.IsEmpty())
					{
						Default->SetStringField(TEXT("pin"), AltPin);
						Report.bApplied = true;
						Report.DeprecatedFieldsSeen.Add(TEXT("defaults.pinToken"));
						Report.Notes.Add(TEXT("Aliased defaults[i].pinToken -> defaults[i].pin"));
					}
				}
				RewrittenDefaults.Add(MakeShared<FJsonValueObject>(Default));
			}

			if (bRewroteDefaults)
			{
				Args->SetArrayField(TEXT("variables"), VariablesArr);
				Args->SetArrayField(TEXT("defaults"), RewrittenDefaults);
			}
		}
	}

	/**
	 * Models often pass /Game/Folder/Asset without the required .Asset suffix.
	 * Canonical form for Unreal object paths is /Game/.../Name.Name (package path + exported object name).
	 */
	void NormalizeBlueprintObjectPath(FString& Path)
	{
		UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(Path);
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty() || Path.Contains(TEXT("..")))
		{
			return;
		}
		if (Path.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase))
		{
			Path.LeftChopInline(7);
			Path.TrimEndInline();
		}
		int32 LastSlash = INDEX_NONE;
		if (!Path.FindLastChar(TEXT('/'), LastSlash))
		{
			return;
		}
		const FString Leaf = Path.Mid(LastSlash + 1);
		if (Leaf.IsEmpty() || Leaf.Contains(TEXT(".")))
		{
			return;
		}
		Path.Reserve(Path.Len() + 1 + Leaf.Len());
		Path.Append(TEXT(".")).Append(Leaf);
	}

	static bool IsGameWritableBlueprintObjectPath(const FString& BlueprintObjectPath)
	{
		if (BlueprintObjectPath.IsEmpty() || BlueprintObjectPath.Contains(TEXT("..")))
		{
			return false;
		}
		FString Tmp = BlueprintObjectPath;
		NormalizeBlueprintObjectPath(Tmp);
		const FString Pkg = FPackageName::ObjectPathToPackageName(Tmp);
		return Pkg.StartsWith(TEXT("/Game/")) || Pkg == TEXT("/Game");
	}

	static UBlueprint* LoadBlueprint(const FString& PathIn)
	{
		FString Path = PathIn;
		NormalizeBlueprintObjectPath(Path);
		// Some malformed blueprint assets in harness content can assert during load
		// (e.g. GeneratedClass == null). Check registry tags first and fail closed.
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(Path));
			if (AD.IsValid())
			{
				FString GeneratedClassTag;
				if (!AD.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClassTag)
					|| GeneratedClassTag.IsEmpty()
					|| GeneratedClassTag.Equals(TEXT("None"), ESearchCase::IgnoreCase)
					|| GeneratedClassTag.Equals(TEXT("null"), ESearchCase::IgnoreCase))
				{
					return nullptr;
				}
			}
		}
		if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path))
		{
			return BP;
		}
		return Cast<UBlueprint>(FSoftObjectPath(Path).TryLoad());
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

	static UBlueprint* TryCreateBlueprintAsset(
		const FString& BlueprintObjectPath,
		const FString& InParentClassPath,
		TArray<FIrError>& Errors)
	{
		FString CanonPath = BlueprintObjectPath;
		NormalizeBlueprintObjectPath(CanonPath);
		FString PackageName;
		FString AssetShortName;
		if (!CanonPath.Split(TEXT("."), &PackageName, &AssetShortName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			AddError(
				Errors,
				TEXT("invalid_path"),
				TEXT("$.blueprint_path"),
				TEXT("Expected object path /Game/.../Asset or /Game/.../Asset.Asset"),
				TEXT("Example: /Game/Temp/MyBp or /Game/Temp/MyBp.MyBp"));
			return nullptr;
		}
		if (!IsGameWritableBlueprintObjectPath(CanonPath))
		{
			AddError(
				Errors,
				TEXT("invalid_path"),
				TEXT("$.blueprint_path"),
				TEXT("New Blueprint assets must live under /Game"),
				TEXT(""));
			return nullptr;
		}
		UClass* ParentClass = AActor::StaticClass();
		if (!InParentClassPath.IsEmpty())
		{
			ParentClass = FindObject<UClass>(nullptr, *InParentClassPath);
			if (!ParentClass)
			{
				ParentClass = LoadObject<UClass>(nullptr, *InParentClassPath);
			}
			if (!ParentClass)
			{
				AddError(
					Errors,
					TEXT("invalid_class"),
					TEXT("$.parent_class"),
					TEXT("Could not resolve parent_class"),
					TEXT("Use /Script/Engine.Actor or another native class path"));
				return nullptr;
			}
		}
		UPackage* Pkg = CreatePackage(*PackageName);
		if (!Pkg)
		{
			AddError(Errors, TEXT("create_failed"), TEXT("$.blueprint_path"), TEXT("CreatePackage failed"), TEXT(""));
			return nullptr;
		}
		UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			Pkg,
			FName(*AssetShortName),
			BPTYPE_Normal,
			NAME_None);
		if (!NewBP)
		{
			AddError(
				Errors,
				TEXT("create_failed"),
				TEXT("$.blueprint_path"),
				TEXT("CreateBlueprint failed (asset may already exist with a different type)"),
				TEXT("Delete the package or use a unique blueprint_path"));
			return nullptr;
		}
		NewBP->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(NewBP);
		return NewBP;
	}

	static FUnrealAiToolInvocationResult MakeIrErrorResult(
		const TCHAR* Status,
		const TCHAR* Headline,
		const TArray<FIrError>& Errors,
		const TArray<FString>& NormalizationNotes = TArray<FString>(),
		const TArray<FString>& DeprecatedFields = TArray<FString>(),
		TSharedPtr<FJsonObject> SuggestedArguments = nullptr,
		const TCHAR* SuggestedToolId = TEXT(""))
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), false);
		O->SetStringField(TEXT("status"), Status);
		O->SetStringField(TEXT("error"), Headline);
		if (NormalizationNotes.Num() > 0 || DeprecatedFields.Num() > 0)
		{
			O->SetBoolField(TEXT("normalization_applied"), true);
		}
		if (NormalizationNotes.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> NotesArr;
			for (const FString& N : NormalizationNotes)
			{
				NotesArr.Add(MakeShared<FJsonValueString>(N));
			}
			O->SetArrayField(TEXT("normalization_notes"), NotesArr);
		}
		if (DeprecatedFields.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> DepArr;
			for (const FString& N : DeprecatedFields)
			{
				DepArr.Add(MakeShared<FJsonValueString>(N));
			}
			O->SetArrayField(TEXT("deprecated_fields_seen"), DepArr);
		}
		if (SuggestedArguments.IsValid() && SuggestedArguments->HasField(TEXT("blueprint_path")))
		{
			// Contract shape for model retry: { "tool_id": "...", "arguments": { ... } }
			TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
			if (SuggestedToolId && SuggestedToolId[0] != '\0')
			{
				Suggested->SetStringField(TEXT("tool_id"), SuggestedToolId);
			}
			Suggested->SetObjectField(TEXT("arguments"), SuggestedArguments);
			O->SetObjectField(TEXT("suggested_correct_call"), Suggested);
		}

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
				TEXT("e.g. /Game/BP_MyActor.BP_MyActor or shorthand /Game/BP_MyActor"));
		}
		else
		{
			NormalizeBlueprintObjectPath(OutIr.BlueprintPath);
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
						TEXT("Use node.pin syntax, e.g. begin_play.then -> branch.execute"));
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

		bool CreateIfMissing = false;
		if (Args->TryGetBoolField(TEXT("create_if_missing"), CreateIfMissing))
		{
			OutIr.bCreateIfMissing = CreateIfMissing;
		}
		Args->TryGetStringField(TEXT("parent_class"), OutIr.ParentClassPath);

		bool AutoLayout = true;
		if (Args->TryGetBoolField(TEXT("auto_layout"), AutoLayout))
		{
			OutIr.bAutoLayout = AutoLayout;
		}

		Args->TryGetStringField(TEXT("merge_policy"), OutIr.MergePolicy);
		OutIr.MergePolicy.TrimStartAndEndInline();
		if (!OutIr.MergePolicy.IsEmpty()
			&& !OutIr.MergePolicy.Equals(TEXT("create_new"), ESearchCase::IgnoreCase)
			&& !OutIr.MergePolicy.Equals(TEXT("append_to_existing"), ESearchCase::IgnoreCase))
		{
			AddError(
				OutErrors,
				TEXT("invalid_value"),
				TEXT("$.merge_policy"),
				TEXT("merge_policy must be create_new or append_to_existing"),
				TEXT("Omit merge_policy to use defaults (append on EventGraph, create_new on function graphs)"));
		}

		Args->TryGetStringField(TEXT("layout_scope"), OutIr.LayoutScope);
		OutIr.LayoutScope.TrimStartAndEndInline();
		if (OutIr.LayoutScope.IsEmpty())
		{
			OutIr.LayoutScope = TEXT("ir_nodes");
		}
		if (!OutIr.LayoutScope.Equals(TEXT("ir_nodes"), ESearchCase::IgnoreCase)
			&& !OutIr.LayoutScope.Equals(TEXT("full_graph"), ESearchCase::IgnoreCase))
		{
			AddError(
				OutErrors,
				TEXT("invalid_value"),
				TEXT("$.layout_scope"),
				TEXT("layout_scope must be ir_nodes or full_graph"),
				TEXT("ir_nodes: layout only nodes from this IR; full_graph: run formatter on the whole graph"));
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
			if (Mn == FName(TEXT("ReceiveTick")))
			{
				OutOp = TEXT("event_tick");
				OutObj->SetStringField(TEXT("op"), OutOp);
				return;
			}
			if (Mn == FName(TEXT("ReceiveActorBeginOverlap")))
			{
				OutOp = TEXT("event_actor_begin_overlap");
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

	/** Maps common LLM / doc aliases to real K2 pin names (execute/then/else/condition). */
	static FString NormalizeBlueprintIrPinToken(const FString& PinName)
	{
		FString P = PinName;
		P.TrimStartAndEndInline();
		if (P.IsEmpty())
		{
			return P;
		}
		if (P.Equals(TEXT("Exec"), ESearchCase::IgnoreCase) || P.Equals(TEXT("Execute"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Execute.ToString();
		}
		if (P.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Then.ToString();
		}
		// Common alias for exec input pins on nodes like Branch.
		if (P.Equals(TEXT("Input"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Execute.ToString();
		}
		if (P.Equals(TEXT("Then"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Then.ToString();
		}
		if (P.Equals(TEXT("Else"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Else.ToString();
		}
		if (P.Equals(TEXT("Condition"), ESearchCase::IgnoreCase))
		{
			return UEdGraphSchema_K2::PN_Condition.ToString();
		}
		return P;
	}

	static bool IsUbergraph(UBlueprint* BP, UEdGraph* Graph)
	{
		if (!BP || !Graph)
		{
			return false;
		}
		for (const TObjectPtr<UEdGraph>& G : BP->UbergraphPages)
		{
			if (G.Get() == Graph)
			{
				return true;
			}
		}
		return false;
	}

	static FString ResolveEffectiveMergePolicy(const FBlueprintIr& Ir, UBlueprint* BP, UEdGraph* Graph)
	{
		if (!Ir.MergePolicy.IsEmpty())
		{
			return Ir.MergePolicy;
		}
		return IsUbergraph(BP, Graph) ? TEXT("append_to_existing") : TEXT("create_new");
	}

	static bool IsBuiltinEventOp(const FString& OpLower, FName& OutMember)
	{
		if (OpLower == TEXT("event_begin_play"))
		{
			OutMember = FName(TEXT("ReceiveBeginPlay"));
			return true;
		}
		if (OpLower == TEXT("event_tick"))
		{
			OutMember = FName(TEXT("ReceiveTick"));
			return true;
		}
		if (OpLower == TEXT("event_actor_begin_overlap"))
		{
			OutMember = FName(TEXT("ReceiveActorBeginOverlap"));
			return true;
		}
		return false;
	}

	static void CollectEventsByMember(UEdGraph* Graph, FName MemberName, TArray<UK2Node_Event*>& Out)
	{
		Out.Reset();
		if (!Graph)
		{
			return;
		}
		for (UEdGraphNode* N : Graph->Nodes)
		{
			UK2Node_Event* Ev = Cast<UK2Node_Event>(N);
			if (!Ev || Ev->EventReference.GetMemberName() != MemberName)
			{
				continue;
			}
			Out.Add(Ev);
		}
	}

	static void SortEventsDeterministic(TArray<UK2Node_Event*>& Arr)
	{
		Arr.Sort([](const UK2Node_Event& A, const UK2Node_Event& B) {
			if (A.NodePosY != B.NodePosY)
			{
				return A.NodePosY < B.NodePosY;
			}
			if (A.NodePosX != B.NodePosX)
			{
				return A.NodePosX < B.NodePosX;
			}
			return A.NodeGuid < B.NodeGuid;
		});
	}

	static UEdGraphPin* FindPrimaryExecForwardOutPin(UEdGraphNode* N)
	{
		if (!N)
		{
			return nullptr;
		}
		if (UK2Node_Event* Ev = Cast<UK2Node_Event>(N))
		{
			return Ev->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		}
		if (UK2Node_CustomEvent* Ce = Cast<UK2Node_CustomEvent>(N))
		{
			return Ce->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		}
		if (UK2Node_CallFunction* Cf = Cast<UK2Node_CallFunction>(N))
		{
			if (UFunction* Fn = Cf->GetTargetFunction())
			{
				if (Fn->GetName() == TEXT("Delay"))
				{
					return Cf->FindPin(TEXT("Completed"), EGPD_Output);
				}
			}
			return Cf->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		}
		if (UK2Node_IfThenElse* Br = Cast<UK2Node_IfThenElse>(N))
		{
			return Br->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		}
		if (UK2Node_ExecutionSequence* Seq = Cast<UK2Node_ExecutionSequence>(N))
		{
			if (UEdGraphPin* P = Seq->FindPin(TEXT("Then_0"), EGPD_Output))
			{
				return P;
			}
			for (UEdGraphPin* P : Seq->Pins)
			{
				if (P && P->Direction == EGPD_Output && P->PinName.ToString().StartsWith(TEXT("Then")))
				{
					return P;
				}
			}
		}
		return nullptr;
	}

	static UEdGraphNode* FindExecChainTailFromEvent(UK2Node_Event* Ev, TArray<FString>& Warnings)
	{
		if (!Ev)
		{
			return nullptr;
		}
		UEdGraphNode* Cur = Ev;
		TSet<UEdGraphNode*> Visited;
		int32 Guard = 0;
		while (Cur && Guard++ < 4096)
		{
			if (Visited.Contains(Cur))
			{
				Warnings.Add(TEXT("exec_tail_walk_cycle"));
				return Cur;
			}
			Visited.Add(Cur);
			UEdGraphPin* OutExec = FindPrimaryExecForwardOutPin(Cur);
			if (!OutExec || OutExec->LinkedTo.Num() == 0)
			{
				return Cur;
			}
			UEdGraphPin* NextIn = OutExec->LinkedTo[0];
			Cur = NextIn ? NextIn->GetOwningNode() : nullptr;
		}
		if (Guard >= 4096)
		{
			Warnings.Add(TEXT("exec_tail_walk_limit"));
		}
		return Cur;
	}

	static UEdGraphNode* CreateNodeFromDecl(
		UBlueprint* BP,
		UEdGraph* Graph,
		const FIrNodeDecl& D,
		TArray<FIrError>& Errors,
		bool bReuseCustomEventsByName)
	{
		if (!BP || !Graph)
		{
			return nullptr;
		}

		const FString Op = D.Op.ToLower();
		UEdGraphNode* Created = nullptr;

		if (Op == TEXT("event_begin_play"))
		{
			// Reuse existing BeginPlay event to avoid duplicate-override failures.
			for (UEdGraphNode* Existing : Graph->Nodes)
			{
				UK2Node_Event* Ev = Cast<UK2Node_Event>(Existing);
				if (!Ev)
				{
					continue;
				}
				if (Ev->EventReference.GetMemberName() == FName(TEXT("ReceiveBeginPlay")))
				{
					return Ev;
				}
			}
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
		else if (Op == TEXT("event_tick"))
		{
			// Reuse existing Tick event to avoid duplicate-override failures.
			for (UEdGraphNode* Existing : Graph->Nodes)
			{
				UK2Node_Event* Ev = Cast<UK2Node_Event>(Existing);
				if (!Ev)
				{
					continue;
				}
				if (Ev->EventReference.GetMemberName() == FName(TEXT("ReceiveTick")))
				{
					return Ev;
				}
			}
			UK2Node_Event* N = NewObject<UK2Node_Event>(Graph);
			N->EventReference.SetExternalMember(FName(TEXT("ReceiveTick")), AActor::StaticClass());
			N->bOverrideFunction = true;
			Graph->AddNode(N, true, false);
			N->NodePosX = D.X;
			N->NodePosY = D.Y;
			N->CreateNewGuid();
			N->PostPlacedNewNode();
			N->AllocateDefaultPins();
			Created = N;
		}
		else if (Op == TEXT("event_actor_begin_overlap"))
		{
			// Reuse existing overlap event when present.
			for (UEdGraphNode* Existing : Graph->Nodes)
			{
				UK2Node_Event* Ev = Cast<UK2Node_Event>(Existing);
				if (!Ev)
				{
					continue;
				}
				if (Ev->EventReference.GetMemberName() == FName(TEXT("ReceiveActorBeginOverlap")))
				{
					return Ev;
				}
			}
			UK2Node_Event* N = NewObject<UK2Node_Event>(Graph);
			N->EventReference.SetExternalMember(FName(TEXT("ReceiveActorBeginOverlap")), AActor::StaticClass());
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
			if (bReuseCustomEventsByName)
			{
				for (UEdGraphNode* Existing : Graph->Nodes)
				{
					UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Existing);
					if (CE && CE->CustomFunctionName.ToString().Equals(D.Name, ESearchCase::IgnoreCase))
					{
						return CE;
					}
				}
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
				TEXT("Supported ops: event_begin_play, event_tick, custom_event, branch, sequence, call_function, delay, get_variable, set_variable, dynamic_cast. For gameplay actions, use call_function with class_path + function_name."));
			return nullptr;
		}

		return Created;
	}
}

void UnrealAiNormalizeBlueprintObjectPath(FString& BlueprintObjectPath)
{
	UnrealAiBlueprintToolsPriv::NormalizeBlueprintObjectPath(BlueprintObjectPath);
}

static FString UnrealAiBlueprintLoadHint(const FString& BlueprintPath)
{
	return FString::Printf(
		TEXT("Could not load Blueprint '%s'. Expected a Blueprint asset object path under /Game (for example /Game/Blueprints/MyBP or /Game/Blueprints/MyBP.MyBP). If this is a new asset, create it first with asset_create or set create_if_missing:true in blueprint_apply_ir."),
		*BlueprintPath);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintCompile(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("blueprint_path is required. Discover Blueprints with asset_index_fuzzy_search (query + path_prefix) or asset_registry_query with path_filter/class_name."),
			TEXT("blueprint_compile"),
			SuggestedArgs);
	}
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(UnrealAiBlueprintLoadHint(Path));
	}
	bool bFormatGraphs = false;
	Args->TryGetBoolField(TEXT("format_graphs"), bFormatGraphs);
	const bool bFormatterLoaded = UnrealAiBlueprintFormatterBridge::EnsureFormatterModuleLoaded(nullptr);
	int32 GraphsFormatted = 0;
	if (bFormatGraphs && bFormatterLoaded)
	{
		const FScopedTransaction Txn(
			NSLOCTEXT("UnrealAiEditor", "TxnBpCompileFormat", "Unreal AI: format Blueprint graphs"));
		BP->Modify();
		GraphsFormatted = UnrealAiBlueprintFormatterBridge::TryLayoutAllScriptGraphs(BP);
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
	O->SetBoolField(TEXT("format_graphs_requested"), bFormatGraphs);
	O->SetNumberField(TEXT("graphs_auto_formatted"), static_cast<double>(GraphsFormatted));
	O->SetBoolField(TEXT("formatter_available"), bFormatterLoaded);
	if (bFormatGraphs && !bFormatterLoaded)
	{
		O->SetStringField(TEXT("formatter_hint"), UnrealAiBlueprintFormatterBridge::FormatterInstallHint());
	}

	const FString ToolMarkdown = FString::Printf(
		TEXT("### Blueprint compile\n- Errors: %d\n- Warnings: %d\n- Blueprint status: %d\n"),
		ErrCount,
		WarnCount,
		static_cast<int32>(BP->Status));
	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, FString(), ToolMarkdown));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintExportIr(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiBlueprintToolsPriv;
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
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
		return UnrealAiToolJson::Error(UnrealAiBlueprintLoadHint(Path));
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
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("blueprint_path"), Path);
		SuggestedArgs->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("Graph not found. Call blueprint_get_graph_summary first to discover valid graph names, then retry blueprint_export_ir with one concrete graph_name."),
			TEXT("blueprint_export_ir"),
			SuggestedArgs);
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
	const FString ToolMarkdown = FString::Printf(TEXT("### Blueprint IR export\n- Graph: **%s**\n"), *Graph->GetName());
	return UnrealAiToolJson::OkWithEditorPresentation(
		Out,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, Graph->GetName(), ToolMarkdown));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintApplyIr(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiBlueprintToolsPriv;

	FIrNormalizationReport Normalization;
	NormalizeBlueprintIrArgs(Args, Normalization);

	FBlueprintIr Ir;
	TArray<FIrError> ParseErrors;
	if (!TryParseIr(Args, Ir, ParseErrors))
	{
		return MakeIrErrorResult(
			TEXT("invalid_ir"),
			TEXT("Invalid blueprint IR (check required keys/types in error_details)."),
			ParseErrors,
			Normalization.Notes,
			Normalization.DeprecatedFieldsSeen,
			Normalization.bApplied ? Args : nullptr,
			TEXT("blueprint_apply_ir"));
	}

	bool bCreatedBlueprint = false;
	UBlueprint* BP = LoadBlueprint(Ir.BlueprintPath);
	if (!BP && Ir.bCreateIfMissing)
	{
		TArray<FIrError> CreateErrors;
		BP = TryCreateBlueprintAsset(Ir.BlueprintPath, Ir.ParentClassPath, CreateErrors);
		if (!BP)
		{
			return MakeIrErrorResult(
				TEXT("create_failed"),
				TEXT("Blueprint create failed"),
				CreateErrors,
				Normalization.Notes,
				Normalization.DeprecatedFieldsSeen);
		}
		bCreatedBlueprint = true;
	}
	if (!BP)
	{
		TArray<FIrError> Errors;
		AddError(
			Errors,
			TEXT("asset_not_found"),
			TEXT("$.blueprint_path"),
			TEXT("Could not load Blueprint"),
			TEXT("Set create_if_missing:true (optional parent_class /Script/Engine.Actor) or create the asset with asset_create, then retry."));
		TSharedPtr<FJsonObject> SuggestedArgs = nullptr;
		if (!Ir.bCreateIfMissing)
		{
			// Deterministic repair: flip create_if_missing so the Blueprint exists for the apply.
			SuggestedArgs = Args;
			SuggestedArgs->SetBoolField(TEXT("create_if_missing"), true);
			if (!SuggestedArgs->HasField(TEXT("parent_class")))
			{
				SuggestedArgs->SetStringField(TEXT("parent_class"), TEXT("/Script/Engine.Actor"));
			}
		}
		return MakeIrErrorResult(
			TEXT("asset_not_found"),
			TEXT("Blueprint load failed"),
			Errors,
			Normalization.Notes,
			Normalization.DeprecatedFieldsSeen,
			SuggestedArgs,
			TEXT("blueprint_apply_ir"));
	}
	if (!Ir.ParentClassPath.IsEmpty() && BP->ParentClass == nullptr)
	{
		UClass* DesiredParent = FindObject<UClass>(nullptr, *Ir.ParentClassPath);
		if (!DesiredParent)
		{
			DesiredParent = LoadObject<UClass>(nullptr, *Ir.ParentClassPath);
		}
		if (!DesiredParent)
		{
			TArray<FIrError> Errors;
			AddError(
				Errors,
				TEXT("invalid_class"),
				TEXT("$.parent_class"),
				TEXT("Could not resolve parent_class"),
				TEXT("Use a native class path like /Script/Engine.Actor or /Script/Engine.Character"));
			return MakeIrErrorResult(
				TEXT("invalid_class"),
				TEXT("Parent class resolution failed"),
				Errors,
				Normalization.Notes,
				Normalization.DeprecatedFieldsSeen);
		}
		BP->ParentClass = DesiredParent;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}

	UEdGraph* Graph = FindGraphByName(BP, Ir.GraphName);
	if (!Graph && Ir.GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
	{
		UEdGraph* NewUbergraph = FBlueprintEditorUtils::CreateNewGraph(
			BP,
			FName(TEXT("EventGraph")),
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (NewUbergraph)
		{
			FBlueprintEditorUtils::AddUbergraphPage(BP, NewUbergraph);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			Graph = NewUbergraph;
		}
	}
	if (!Graph)
	{
		TArray<FIrError> Errors;
		AddError(
			Errors,
			TEXT("graph_not_found"),
			TEXT("$.graph_name"),
			TEXT("Graph not found on Blueprint"),
			TEXT("For v1 use EventGraph (default)"));
		TSharedPtr<FJsonObject> SuggestedArgs = nullptr;
		if (!Ir.GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
		{
			// Deterministic repair: try EventGraph since the apply path can auto-create it.
			SuggestedArgs = Args;
			SuggestedArgs->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
		}
		return MakeIrErrorResult(
			TEXT("graph_not_found"),
			TEXT("Graph resolution failed"),
			Errors,
			Normalization.Notes,
			Normalization.DeprecatedFieldsSeen,
			SuggestedArgs,
			TEXT("blueprint_apply_ir"));
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

	const FString EffectiveMergePolicy = ResolveEffectiveMergePolicy(Ir, BP, Graph);
	TMap<FString, FEventMergePlan> MergePlans;
	TArray<FString> MergeWarnings;

	if (EffectiveMergePolicy.Equals(TEXT("append_to_existing"), ESearchCase::IgnoreCase))
	{
		TSet<FName> IrMergedMembers;
		for (const FIrNodeDecl& D : Ir.Nodes)
		{
			const FString Op = D.Op.ToLower();
			FName MemberName;
			if (!IsBuiltinEventOp(Op, MemberName))
			{
				continue;
			}
			TArray<UK2Node_Event*> Matches;
			CollectEventsByMember(Graph, MemberName, Matches);
			SortEventsDeterministic(Matches);
			if (Matches.Num() == 0)
			{
				continue;
			}
			if (IrMergedMembers.Contains(MemberName))
			{
				MergeWarnings.Add(FString::Printf(
					TEXT("append_skipped_duplicate_ir_event_op:%s:%s"),
					*D.NodeId,
					*MemberName.ToString()));
				continue;
			}
			IrMergedMembers.Add(MemberName);

			FEventMergePlan Plan;
			Plan.bMerged = true;
			Plan.ChosenEvent = Matches[0];
			Plan.MemberName = MemberName;
			Plan.DuplicateAnchors = FMath::Max(0, Matches.Num() - 1);
			Plan.TailNode = FindExecChainTailFromEvent(Plan.ChosenEvent, MergeWarnings);
			if (!Plan.TailNode)
			{
				Plan.TailNode = Plan.ChosenEvent;
			}
			MergePlans.Add(D.NodeId, MoveTemp(Plan));
			if (Matches.Num() > 1)
			{
				MergeWarnings.Add(FString::Printf(
					TEXT("duplicate_event_anchors_in_graph:%s:%d"),
					*MemberName.ToString(),
					Matches.Num()));
			}
		}
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
		if (const FEventMergePlan* Plan = MergePlans.Find(D.NodeId))
		{
			if (Plan->bMerged && Plan->ChosenEvent)
			{
				NodeById.Add(D.NodeId, Plan->ChosenEvent);
				continue;
			}
		}
		const bool bReuseCustomEvents = EffectiveMergePolicy.Equals(TEXT("append_to_existing"), ESearchCase::IgnoreCase);
		if (UEdGraphNode* N = CreateNodeFromDecl(BP, Graph, D, BuildErrors, bReuseCustomEvents))
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
		FromPinName = NormalizeBlueprintIrPinToken(FromPinName);
		ToPinName = NormalizeBlueprintIrPinToken(ToPinName);
		if (const FEventMergePlan* MergeFrom = MergePlans.Find(FromNodeId))
		{
			if (MergeFrom->bMerged && MergeFrom->TailNode)
			{
				const FString ThenStr = UEdGraphSchema_K2::PN_Then.ToString();
				if (FromPinName.Equals(ThenStr, ESearchCase::IgnoreCase))
				{
					FromNode = MergeFrom->TailNode;
				}
			}
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
			// Models sometimes list links as input->output; try the opposite direction once.
			if (!Schema->TryCreateConnection(B, A))
			{
				AddError(
					BuildErrors,
					TEXT("type_mismatch"),
					TEXT("$.links"),
					TEXT("Could not connect pins"),
					TEXT("Use K2 names: execute, then, else, condition (aliases Exec/Then accepted). Direction is from output pin to input pin."));
			}
		}
	}

	if (BuildErrors.Num() > 0)
	{
		TSharedPtr<FJsonObject> SuggestedArgs = nullptr;
		const TCHAR* SuggestedToolId = TEXT("");
		bool bHasUnsupportedOp = false;
		for (const FIrError& E : BuildErrors)
		{
			if (E.Code.Equals(TEXT("unsupported_op"), ESearchCase::CaseSensitive))
			{
				bHasUnsupportedOp = true;
				break;
			}
		}
		if (bHasUnsupportedOp)
		{
			// Deterministic recovery for unknown op loops:
			// ask model to read existing graph IR first, then re-apply with supported ops only.
			SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("blueprint_path"), Ir.BlueprintPath);
			SuggestedToolId = TEXT("blueprint_export_ir");
		}
		return MakeIrErrorResult(
			TEXT("apply_failed"),
			TEXT("Blueprint IR apply failed"),
			BuildErrors,
			Normalization.Notes,
			Normalization.DeprecatedFieldsSeen,
			SuggestedArgs,
			SuggestedToolId);
	}

	TArray<UEdGraphNode*> MaterializedNodes;
	NodeById.GenerateValueArray(MaterializedNodes);
	TArray<FUnrealBlueprintIrNodeLayoutHint> LayoutHints;
	LayoutHints.Reserve(Ir.Nodes.Num());
	for (const FIrNodeDecl& D : Ir.Nodes)
	{
		FUnrealBlueprintIrNodeLayoutHint H;
		H.NodeId = D.NodeId;
		H.X = D.X;
		H.Y = D.Y;
		LayoutHints.Add(MoveTemp(H));
	}
	UnrealAiBlueprintFormatterBridge::EnsureFormatterModuleLoaded(nullptr);
	const bool bFullGraphLayout = Ir.LayoutScope.Equals(TEXT("full_graph"), ESearchCase::IgnoreCase);
	FUnrealBlueprintGraphFormatResult FormatResult;
	if (Ir.bAutoLayout)
	{
		FormatResult = bFullGraphLayout
			? UnrealAiBlueprintFormatterBridge::TryLayoutEntireGraph(Graph, true)
			: UnrealAiBlueprintFormatterBridge::TryLayoutAfterAiIrApply(Graph, MaterializedNodes, LayoutHints, true);
	}
	const bool bFormatterAvailable = UnrealAiBlueprintFormatterBridge::IsFormatterModuleReady();
	const bool bLayoutApplied = FormatResult.NodesPositioned > 0;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), BP->Status != BS_Error);
	O->SetStringField(TEXT("blueprint_path"), Ir.BlueprintPath);
	O->SetStringField(TEXT("graph_name"), Ir.GraphName);
	O->SetNumberField(TEXT("node_count"), static_cast<double>(NodeById.Num()));
	O->SetNumberField(TEXT("link_count"), static_cast<double>(Ir.Links.Num()));
	O->SetNumberField(TEXT("blueprint_status"), static_cast<double>(static_cast<int32>(BP->Status)));
	O->SetBoolField(TEXT("created_blueprint"), bCreatedBlueprint);
	O->SetStringField(TEXT("merge_policy_used"), EffectiveMergePolicy);
	O->SetStringField(TEXT("layout_scope_used"), Ir.LayoutScope);
	O->SetBoolField(TEXT("formatter_available"), bFormatterAvailable);
	O->SetBoolField(TEXT("layout_applied"), bLayoutApplied);
	O->SetBoolField(TEXT("auto_layout_requested"), Ir.bAutoLayout);
	O->SetNumberField(TEXT("layout_nodes_positioned"), static_cast<double>(FormatResult.NodesPositioned));
	if (Normalization.bApplied)
	{
		O->SetBoolField(TEXT("normalization_applied"), true);
		if (Normalization.Notes.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> NotesArr;
			for (const FString& N : Normalization.Notes)
			{
				NotesArr.Add(MakeShared<FJsonValueString>(N));
			}
			O->SetArrayField(TEXT("normalization_notes"), NotesArr);
		}
		if (Normalization.DeprecatedFieldsSeen.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> DepArr;
			for (const FString& N : Normalization.DeprecatedFieldsSeen)
			{
				DepArr.Add(MakeShareable(new FJsonValueString(N)));
			}
			O->SetArrayField(TEXT("deprecated_fields_seen"), DepArr);
		}
	}
	if (!bFormatterAvailable && Ir.bAutoLayout)
	{
		O->SetStringField(TEXT("formatter_hint"), UnrealAiBlueprintFormatterBridge::FormatterInstallHint());
	}
	if (FormatResult.Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnArr;
		for (const FString& W : FormatResult.Warnings)
		{
			WarnArr.Add(MakeShareable(new FJsonValueString(W)));
		}
		O->SetArrayField(TEXT("layout_warnings"), WarnArr);
	}
	{
		TArray<TSharedPtr<FJsonValue>> MergeWarnJson;
		for (const FString& W : MergeWarnings)
		{
			MergeWarnJson.Add(MakeShareable(new FJsonValueString(W)));
		}
		if (MergeWarnJson.Num() > 0)
		{
			O->SetArrayField(TEXT("merge_warnings"), MergeWarnJson);
		}
	}
	{
		TArray<TSharedPtr<FJsonValue>> AnchorsJson;
		for (const auto& Pair : MergePlans)
		{
			if (Pair.Value.bMerged && Pair.Value.ChosenEvent)
			{
				AnchorsJson.Add(MakeShareable(new FJsonValueString(
					FString::Printf(TEXT("%s:%s"), *Pair.Key, *Pair.Value.MemberName.ToString()))));
			}
		}
		if (AnchorsJson.Num() > 0)
		{
			O->SetArrayField(TEXT("anchors_reused"), AnchorsJson);
		}
	}
	const FString ToolMarkdown = FString::Printf(
		TEXT("### Blueprint IR apply\n- Nodes created: %d\n- Links created: %d\n- Blueprint status: %d\n"),
		NodeById.Num(),
		Ir.Links.Num(),
		static_cast<int32>(BP->Status));
	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Ir.BlueprintPath, Ir.GraphName, ToolMarkdown));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintFormatGraph(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiBlueprintToolsPriv;
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required"));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	NormalizeBlueprintObjectPath(Path);
	UBlueprint* BP = LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(UnrealAiBlueprintLoadHint(Path));
	}
	UEdGraph* Graph = nullptr;
	if (!GraphName.IsEmpty())
	{
		Graph = FindGraphByName(BP, GraphName);
	}
	else
	{
		Graph = BP->UbergraphPages.Num() > 0 ? BP->UbergraphPages[0].Get() : nullptr;
	}
	if (!Graph)
	{
		return UnrealAiToolJson::Error(TEXT("Graph not found"));
	}

	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnBpFormatGraph", "Unreal AI: format Blueprint graph"));
	BP->Modify();
	Graph->Modify();

	UnrealAiBlueprintFormatterBridge::EnsureFormatterModuleLoaded(nullptr);
	const bool bFmt = UnrealAiBlueprintFormatterBridge::IsFormatterModuleReady();
	const FUnrealBlueprintGraphFormatResult FormatResult = UnrealAiBlueprintFormatterBridge::TryLayoutEntireGraph(Graph, true);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("graph_name"), Graph->GetName());
	O->SetBoolField(TEXT("formatter_available"), bFmt);
	O->SetBoolField(TEXT("layout_applied"), FormatResult.NodesPositioned > 0);
	O->SetNumberField(TEXT("layout_nodes_positioned"), static_cast<double>(FormatResult.NodesPositioned));
	if (!bFmt)
	{
		O->SetStringField(TEXT("formatter_hint"), UnrealAiBlueprintFormatterBridge::FormatterInstallHint());
	}
	if (FormatResult.Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnArr;
		for (const FString& W : FormatResult.Warnings)
		{
			WarnArr.Add(MakeShareable(new FJsonValueString(W)));
		}
		O->SetArrayField(TEXT("layout_warnings"), WarnArr);
	}
	const FString ToolMarkdown = FString::Printf(TEXT("### Blueprint format graph\n- Graph: **%s**\n"), *Graph->GetName());
	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, Graph->GetName(), ToolMarkdown));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("blueprint_path is required. Discover Blueprints with asset_index_fuzzy_search (query + path_prefix) or asset_registry_query with path_filter/class_name."),
			TEXT("blueprint_get_graph_summary"),
			SuggestedArgs);
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(UnrealAiBlueprintLoadHint(Path));
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

	TArray<FString> FirstNames;
	for (const TSharedPtr<FJsonValue>& V : GraphArr)
	{
		if (!V.IsValid())
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!V->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
		{
			continue;
		}
		FString Name;
		if ((*Obj)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			FirstNames.Add(Name);
		}
		if (FirstNames.Num() >= 8)
		{
			break;
		}
	}

	const FString GraphScope = GraphName.IsEmpty() ? TEXT("(all)") : GraphName;
	FString ToolMarkdown = FString::Printf(
		TEXT("### Blueprint graphs\n- Scope: %s\n- Returned graphs: %d\n"),
		*GraphScope,
		GraphArr.Num());
	if (FirstNames.Num() > 0)
	{
		ToolMarkdown += TEXT("- First graphs: ");
		ToolMarkdown += FString::Join(FirstNames, TEXT(", "));
		ToolMarkdown += TEXT("\n");
	}

	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, GraphName, ToolMarkdown));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintOpenGraphTab(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	FString GraphName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty()
		|| !Args->TryGetStringField(TEXT("graph_name"), GraphName) || GraphName.IsEmpty())
	{
		if (Path.IsEmpty())
		{
			Args->TryGetStringField(TEXT("object_path"), Path);
		}
		if (Path.IsEmpty())
		{
			Args->TryGetStringField(TEXT("asset_path"), Path);
		}
		if (GraphName.IsEmpty())
		{
			Args->TryGetStringField(TEXT("graph"), GraphName);
		}
		if (Path.IsEmpty() || GraphName.IsEmpty())
		{
			TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
			SuggestedArgs->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("blueprint_path and graph_name are required (aliases: object_path/asset_path, graph)."),
				TEXT("blueprint_open_graph_tab"),
				SuggestedArgs);
		}
	}
	UnrealAiNormalizeBlueprintObjectPath(Path);
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(UnrealAiBlueprintLoadHint(Path));
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

	const FString ToolMarkdown = FString::Printf(TEXT("### Focus graph\n- Graph: %s\n"), *GraphName);
	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, GraphName, ToolMarkdown));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintAddVariable(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	FString VarName;
	FString TypeStr;
	Args->TryGetStringField(TEXT("blueprint_path"), Path);
	Args->TryGetStringField(TEXT("name"), VarName);
	if (VarName.IsEmpty())
	{
		Args->TryGetStringField(TEXT("variable_name"), VarName);
	}
	Args->TryGetStringField(TEXT("type"), TypeStr);
	if (TypeStr.IsEmpty())
	{
		Args->TryGetStringField(TEXT("variable_type"), TypeStr);
	}
	if (Path.IsEmpty() || VarName.IsEmpty() || TypeStr.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path, name, and type are required (aliases: variable_name, variable_type)."));
	}
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(UnrealAiBlueprintLoadHint(Path));
	}
	const FEdGraphPinType PinType = UnrealAiBlueprintToolsPriv::ParsePinType(TypeStr);
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnBpVar", "Unreal AI: add BP variable"));
	FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	const FString ToolMarkdown = FString::Printf(TEXT("### Blueprint variable\n- Added **%s** (%s)\n"), *VarName, *TypeStr);
	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, FString(), ToolMarkdown));
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

void UnrealAiFocusBlueprintEditor(const FString& BlueprintObjectPath, const FString& GraphName)
{
	using namespace UnrealAiBlueprintToolsPriv;
	if (BlueprintObjectPath.IsEmpty() || !GEditor)
	{
		return;
	}
	UBlueprint* BP = LoadBlueprint(BlueprintObjectPath);
	if (!BP)
	{
		return;
	}
	UnrealAiEditorNavigation::OpenAssetEditorPreferDocked(BP);

	FString FocusGraph = GraphName.TrimStartAndEnd();
	if (FocusGraph.IsEmpty())
	{
		FocusGraph = TEXT("EventGraph");
	}
	if (UEdGraph* G = FindGraphByName(BP, FocusGraph))
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(G);
		return;
	}
	if (BP->UbergraphPages.Num() > 0 && BP->UbergraphPages[0])
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(BP->UbergraphPages[0]);
	}
}
