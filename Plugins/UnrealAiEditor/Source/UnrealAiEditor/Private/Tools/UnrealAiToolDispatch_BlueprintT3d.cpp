#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

#include "Dom/JsonValue.h"
#include "Tools/UnrealAiToolDispatch_ArgRepair.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/Presentation/UnrealAiToolEditorNoteBuilders.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Animation/AnimBlueprint.h"
#include "Engine/Blueprint.h"
#include "Internationalization/Regex.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_Timeline.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "UObject/Class.h"

namespace UnrealAiBlueprintT3d
{
	static bool CollectAndResolvePlaceholders(FString& InOutT3d, FString& OutErr)
	{
		static const FRegexPattern TokenPat(TEXT("__UAI_G_([0-9]{6})__"));
		TArray<FString> OrderedUnique;
		TSet<FString> Seen;
		{
			FRegexMatcher M(TokenPat, InOutT3d);
			while (M.FindNext())
			{
				const FString Digits = M.GetCaptureGroup(1);
				const FString Token = FString::Printf(TEXT("__UAI_G_%s__"), *Digits);
				if (!Seen.Contains(Token))
				{
					Seen.Add(Token);
					OrderedUnique.Add(Token);
				}
			}
		}
		static const FRegexPattern LoosePat(TEXT("__UAI_G_"));
		{
			FRegexMatcher Loose(LoosePat, InOutT3d);
			while (Loose.FindNext())
			{
				const int32 Start = Loose.GetMatchBeginning();
				FString Candidate = InOutT3d.Mid(Start);
				if (Candidate.Len() < 16)
				{
					OutErr = TEXT("Malformed placeholder: incomplete __UAI_G_ token.");
					return false;
				}
				FRegexMatcher Full(TokenPat, Candidate);
				if (!Full.FindNext() || Full.GetMatchBeginning() != 0)
				{
					OutErr = TEXT(
						"Malformed placeholder: expected __UAI_G_NNNNNN__ (six zero-padded digits). "
						"Example: __UAI_G_000001__");
					return false;
				}
			}
		}
		TMap<FString, FString> Repl;
		for (const FString& Tok : OrderedUnique)
		{
			Repl.Add(Tok, LexToString(FGuid::NewGuid()));
		}
		for (const TPair<FString, FString>& P : Repl)
		{
			InOutT3d.ReplaceInline(*P.Key, *P.Value, ESearchCase::CaseSensitive);
		}
		if (InOutT3d.Contains(TEXT("__UAI_G_")))
		{
			OutErr = TEXT("Unresolved or malformed __UAI_G_ placeholder (expected __UAI_G_NNNNNN__ with six digits).");
			return false;
		}
		return true;
	}

	static UEdGraph* ResolveTargetGraph(UBlueprint* BP, const FString& GraphName, FString& OutErr)
	{
		if (!BP)
		{
			OutErr = TEXT("Internal: Blueprint null.");
			return nullptr;
		}
		if (!GraphName.IsEmpty())
		{
			UEdGraph* G = UnrealAiBlueprintTools_FindGraphByName(BP, GraphName);
			if (!G)
			{
				OutErr = FString::Printf(TEXT("Graph not found: %s. Use blueprint_graph_introspect or blueprint_get_graph_summary."), *GraphName);
			}
			return G;
		}
		if (BP->UbergraphPages.Num() > 0)
		{
			return BP->UbergraphPages[0];
		}
		OutErr = TEXT("Blueprint has no EventGraph (ubergraph). Pass graph_name explicitly.");
		return nullptr;
	}

	/** Appended to CanImportNodesFromText=false errors so the model stops retrying T3D on the wrong graph kind. */
	static FString DescribeT3dImportBlockerHint(UEdGraph* Graph, UBlueprint* BP)
	{
		if (!Graph)
		{
			return FString();
		}
		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (Schema && !Schema->IsA(UEdGraphSchema_K2::StaticClass()))
		{
			return TEXT(
				" T3D batch import applies to Kismet script graphs (EdGraphSchema_K2). This graph uses a different schema—use "
				"blueprint_graph_patch on EventGraph (or another K2 graph), or edit animation/state graphs manually.");
		}
		if (BP && BP->IsA(UAnimBlueprint::StaticClass()))
		{
			const FString GN = Graph->GetName();
			if (GN.Contains(TEXT("AnimGraph"), ESearchCase::IgnoreCase) || GN.Contains(TEXT("StateMachine"), ESearchCase::IgnoreCase)
				|| GN.Contains(TEXT("Transition"), ESearchCase::IgnoreCase))
			{
				return TEXT(
					" Animation Blueprint graphs such as AnimGraph/state machines often reject ImportNodesFromText. Prefer EventGraph with blueprint_graph_patch, or state that anim-graph editing is manual.");
			}
		}
		return FString();
	}
} // namespace UnrealAiBlueprintT3d

namespace UnrealAiBlueprintVerifyGraphPriv
{
	static bool IsK2ExecFlowEntry(const UK2Node* K2)
	{
		if (!K2)
		{
			return false;
		}
		return K2->IsA(UK2Node_Event::StaticClass()) || K2->IsA(UK2Node_CustomEvent::StaticClass())
			|| K2->IsA(UK2Node_FunctionEntry::StaticClass());
	}

	static bool SkipOrphanExecChecksForK2Node(const UK2Node* K2)
	{
		if (!K2)
		{
			return true;
		}
		// Reroutes / timelines are poor fits for mandatory exec-in heuristics (often optional / editor-shaped).
		return K2->IsA(UK2Node_Knot::StaticClass()) || K2->IsA(UK2Node_Timeline::StaticClass());
	}
} // namespace UnrealAiBlueprintVerifyGraphPriv

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGraphIntrospect(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required."));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	UnrealAiNormalizeBlueprintObjectPath(Path);
	FString LoadErr;
	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path, &LoadErr);
	if (!BP)
	{
		return UnrealAiToolJson::Error(LoadErr.IsEmpty()
			? FString::Printf(TEXT("Could not load Blueprint at %s. Resolve path via asset search first."), *Path)
			: LoadErr);
	}
	FString GErr;
	UEdGraph* Graph = UnrealAiBlueprintT3d::ResolveTargetGraph(BP, GraphName, GErr);
	if (!Graph)
	{
		return UnrealAiToolJson::Error(GErr);
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("graph_name"), Graph->GetName());
	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N)
		{
			continue;
		}
		TSharedPtr<FJsonObject> No = MakeShared<FJsonObject>();
		No->SetStringField(TEXT("node_guid"), LexToString(N->NodeGuid));
		No->SetStringField(TEXT("class"), N->GetClass() ? N->GetClass()->GetPathName() : FString());
		No->SetStringField(TEXT("title"), N->GetNodeTitle(ENodeTitleType::ListView).ToString());
		TArray<TSharedPtr<FJsonValue>> Pins;
		for (UEdGraphPin* Pin : N->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Pin->PinName.ToString());
			P->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			P->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
			TArray<TSharedPtr<FJsonValue>> LinkedToArr;
			for (UEdGraphPin* LP : Pin->LinkedTo)
			{
				if (!LP)
				{
					continue;
				}
				UEdGraphNode* Peer = LP->GetOwningNode();
				if (!Peer)
				{
					continue;
				}
				TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
				L->SetStringField(TEXT("node_guid"), LexToString(Peer->NodeGuid));
				L->SetStringField(TEXT("pin_name"), LP->PinName.ToString());
				LinkedToArr.Add(MakeShareable(new FJsonValueObject(L.ToSharedRef())));
			}
			P->SetArrayField(TEXT("linked_to"), LinkedToArr);
			Pins.Add(MakeShareable(new FJsonValueObject(P.ToSharedRef())));
		}
		No->SetArrayField(TEXT("pins"), Pins);
		Nodes.Add(MakeShareable(new FJsonValueObject(No.ToSharedRef())));
	}
	O->SetArrayField(TEXT("nodes"), Nodes);
	O->SetNumberField(TEXT("node_count"), static_cast<double>(Nodes.Num()));
	const FString Md = FString::Printf(TEXT("### blueprint_graph_introspect\n- Graph: **%s**\n- Nodes: **%d**\n"), *Graph->GetName(), Nodes.Num());
	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, Graph->GetName(), Md));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintVerifyGraph(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required."));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	UnrealAiNormalizeBlueprintObjectPath(Path);
	FString LoadErr;
	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path, &LoadErr);
	if (!BP)
	{
		return UnrealAiToolJson::Error(LoadErr.IsEmpty()
			? FString::Printf(TEXT("Could not load Blueprint at %s. Resolve path via asset search first."), *Path)
			: LoadErr);
	}
	FString GErr;
	UEdGraph* Graph = UnrealAiBlueprintT3d::ResolveTargetGraph(BP, GraphName, GErr);
	if (!Graph)
	{
		return UnrealAiToolJson::Error(GErr);
	}

	const TArray<TSharedPtr<FJsonValue>>* StepsJson = nullptr;
	TArray<FString> Steps;
	if (Args->TryGetArrayField(TEXT("steps"), StepsJson) && StepsJson)
	{
		for (const TSharedPtr<FJsonValue>& V : *StepsJson)
		{
			if (V.IsValid() && V->Type == EJson::String)
			{
				Steps.Add(V->AsString());
			}
		}
	}
	TArray<FString> OrderedSteps;
	TSet<FString> SeenStep;
	for (FString& S : Steps)
	{
		S.TrimStartAndEndInline();
		if (S.IsEmpty())
		{
			continue;
		}
		if (!SeenStep.Contains(S))
		{
			SeenStep.Add(S);
			OrderedSteps.Add(S);
		}
	}
	if (OrderedSteps.Num() == 0)
	{
		OrderedSteps.Add(TEXT("links"));
	}

	TArray<TSharedPtr<FJsonValue>> Issues;

	auto AddIssue = [&Issues](const FString& Code, const FString& Msg, const FString& NodeGuidOpt = FString())
	{
		TSharedPtr<FJsonObject> I = MakeShared<FJsonObject>();
		I->SetStringField(TEXT("code"), Code);
		I->SetStringField(TEXT("message"), Msg);
		if (!NodeGuidOpt.IsEmpty())
		{
			I->SetStringField(TEXT("node_guid"), NodeGuidOpt);
		}
		Issues.Add(MakeShareable(new FJsonValueObject(I.ToSharedRef())));
	};

	TArray<FString> UnknownSteps;

	for (const FString& Step : OrderedSteps)
	{
		if (Step == TEXT("links"))
		{
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (!N)
				{
					continue;
				}
				for (UEdGraphPin* Pin : N->Pins)
				{
					if (!Pin)
					{
						continue;
					}
					for (UEdGraphPin* L : Pin->LinkedTo)
					{
						if (!L)
						{
							AddIssue(
								TEXT("null_linked_pin"),
								FString::Printf(
									TEXT("Pin %s on node %s has null link entry."),
									*Pin->PinName.ToString(),
									*LexToString(N->NodeGuid)),
								LexToString(N->NodeGuid));
						}
						else
						{
							UEdGraphNode* Owner = L->GetOwningNode();
							if (!Owner || Owner->GetGraph() != Graph)
							{
								AddIssue(
									TEXT("external_graph_link"),
									FString::Printf(
										TEXT("Pin %s links outside the inspected graph."),
										*Pin->PinName.ToString()),
									LexToString(N->NodeGuid));
							}
						}
					}
				}
			}
		}
		else if (Step == TEXT("orphan_pins"))
		{
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (!N)
				{
					continue;
				}
				UK2Node* K2 = Cast<UK2Node>(N);
				if (!K2 || K2->IsNodePure() || UnrealAiBlueprintVerifyGraphPriv::IsK2ExecFlowEntry(K2)
					|| UnrealAiBlueprintVerifyGraphPriv::SkipOrphanExecChecksForK2Node(K2))
				{
					continue;
				}
				for (UEdGraphPin* Pin : K2->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Input)
					{
						continue;
					}
					if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
					{
						continue;
					}
					if (Pin->LinkedTo.Num() > 0)
					{
						continue;
					}
					AddIssue(
						TEXT("orphan_exec_in"),
						FString::Printf(
							TEXT("Required exec input '%s' has no incoming link on node %s (%s)."),
							*Pin->PinName.ToString(),
							*K2->GetName(),
							*LexToString(K2->NodeGuid)),
						LexToString(K2->NodeGuid));
				}
			}
		}
		else if (Step == TEXT("duplicate_node_guids"))
		{
			TMap<FGuid, UEdGraphNode*> ByGuid;
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (!N)
				{
					continue;
				}
				if (UEdGraphNode** Found = ByGuid.Find(N->NodeGuid))
				{
					AddIssue(
						TEXT("duplicate_node_guid"),
						FString::Printf(
							TEXT("NodeGuid %s reused by '%s' and '%s'."),
							*LexToString(N->NodeGuid),
							*(*Found)->GetName(),
							*N->GetName()),
						LexToString(N->NodeGuid));
				}
				else
				{
					ByGuid.Add(N->NodeGuid, N);
				}
			}
		}
		else if (Step == TEXT("dead_exec_outputs"))
		{
			for (UEdGraphNode* N : Graph->Nodes)
			{
				UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(N);
				if (!Call)
				{
					continue;
				}
				for (UEdGraphPin* Pin : Call->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Output)
					{
						continue;
					}
					if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
					{
						continue;
					}
					if (Pin->LinkedTo.Num() == 0)
					{
						AddIssue(
							TEXT("dead_exec_output"),
							FString::Printf(
								TEXT("CallFunction '%s' exec output '%s' has no outgoing link."),
								*Call->GetName(),
								*Pin->PinName.ToString()),
							LexToString(Call->NodeGuid));
					}
				}
			}
		}
		else if (Step == TEXT("pin_type_mismatch"))
		{
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (!N)
				{
					continue;
				}
				for (UEdGraphPin* Pin : N->Pins)
				{
					if (!Pin || Pin->LinkedTo.Num() == 0)
					{
						continue;
					}
					if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						continue;
					}
					for (UEdGraphPin* Other : Pin->LinkedTo)
					{
						if (!Other)
						{
							continue;
						}
						if (Other->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
						{
							continue;
						}
						if (Pin->Direction == EGPD_Output && Other->Direction == EGPD_Input)
						{
							const bool bCat = Pin->PinType.PinCategory == Other->PinType.PinCategory;
							const bool bSub = Pin->PinType.PinSubCategory == Other->PinType.PinSubCategory;
							const bool bObj =
								Pin->PinType.PinSubCategoryObject == Other->PinType.PinSubCategoryObject;
							if (!bCat || !bSub || !bObj)
							{
								AddIssue(
									TEXT("pin_type_mismatch"),
									FString::Printf(
										TEXT("Data link %s.%s -> %s.%s has mismatched pin types (category/subcategory/object)."),
										*N->GetName(),
										*Pin->PinName.ToString(),
										Other->GetOwningNode() ? *Other->GetOwningNode()->GetName() : TEXT("?"),
										*Other->PinName.ToString()),
									LexToString(N->NodeGuid));
							}
						}
					}
				}
			}
		}
		else if (Step == TEXT("trivial_branch_conditions"))
		{
			for (UEdGraphNode* N : Graph->Nodes)
			{
				UK2Node_IfThenElse* Br = Cast<UK2Node_IfThenElse>(N);
				if (!Br)
				{
					continue;
				}
				for (UEdGraphPin* P : Br->Pins)
				{
					if (!P || P->Direction != EGPD_Input)
					{
						continue;
					}
					if (P->PinName != UEdGraphSchema_K2::PN_Condition)
					{
						continue;
					}
					if (P->LinkedTo.Num() > 0)
					{
						continue;
					}
					FString Dv = P->DefaultValue;
					Dv.TrimStartAndEndInline();
					if (Dv.Equals(TEXT("true"), ESearchCase::IgnoreCase))
					{
						AddIssue(
							TEXT("branch_condition_literal_always_true"),
							TEXT("Branch Condition is literal true with no data link (Else branch is dead)."),
							LexToString(Br->NodeGuid));
					}
					else if (Dv.Equals(TEXT("false"), ESearchCase::IgnoreCase))
					{
						AddIssue(
							TEXT("branch_condition_literal_always_false"),
							TEXT("Branch Condition is literal false with no data link (Then branch is dead)."),
							LexToString(Br->NodeGuid));
					}
				}
			}
		}
		else
		{
			UnknownSteps.Add(Step);
		}
	}

	const bool bPass = Issues.Num() == 0;
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), bPass);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("graph_name"), Graph->GetName());
	O->SetArrayField(TEXT("issues"), Issues);
	O->SetNumberField(TEXT("issue_count"), static_cast<double>(Issues.Num()));
	O->SetBoolField(TEXT("passed"), bPass);
	{
		TArray<TSharedPtr<FJsonValue>> UnkJson;
		for (const FString& U : UnknownSteps)
		{
			UnkJson.Add(MakeShareable(new FJsonValueString(U)));
		}
		O->SetArrayField(TEXT("unknown_steps"), UnkJson);
	}
	const FString Md = FString::Printf(
		TEXT("### blueprint_verify_graph\n- Graph issues: **%d** — unrecognized step name(s): **%d**.\n"),
		Issues.Num(),
		UnknownSteps.Num());
	FUnrealAiToolInvocationResult R;
	R.bOk = bPass;
	R.ContentForModel = UnrealAiToolJson::SerializeObject(O);
	R.EditorPresentation = UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, Graph->GetName(), Md);
	return R;
}
