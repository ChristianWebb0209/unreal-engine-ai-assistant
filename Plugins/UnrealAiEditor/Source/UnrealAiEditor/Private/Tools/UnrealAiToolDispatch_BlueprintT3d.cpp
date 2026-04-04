#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

#include "Dom/JsonValue.h"
#include "Tools/UnrealAiToolDispatch_ArgRepair.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/Presentation/UnrealAiToolEditorNoteBuilders.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Animation/AnimBlueprint.h"
#include "Engine/Blueprint.h"
#include "Internationalization/Regex.h"
#include "K2Node.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
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
				"blueprint_graph_patch or blueprint_apply_ir on EventGraph (or another K2 graph), or edit animation/state graphs manually.");
		}
		if (BP && BP->IsA(UAnimBlueprint::StaticClass()))
		{
			const FString GN = Graph->GetName();
			if (GN.Contains(TEXT("AnimGraph"), ESearchCase::IgnoreCase) || GN.Contains(TEXT("StateMachine"), ESearchCase::IgnoreCase)
				|| GN.Contains(TEXT("Transition"), ESearchCase::IgnoreCase))
			{
				return TEXT(
					" Animation Blueprint graphs such as AnimGraph/state machines often reject ImportNodesFromText. Prefer EventGraph with blueprint_graph_patch / blueprint_apply_ir, or state that anim-graph editing is manual.");
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
	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(
			FString::Printf(TEXT("Could not load Blueprint at %s. Resolve path via asset search first."), *Path));
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

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintExportGraphT3d(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required."));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	int32 MaxChars = 500000;
	Args->TryGetNumberField(TEXT("max_chars"), MaxChars);
	MaxChars = FMath::Clamp(MaxChars, 4096, 2000000);

	UnrealAiNormalizeBlueprintObjectPath(Path);
	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(
			FString::Printf(TEXT("Could not load Blueprint at %s. Resolve path via asset search first."), *Path));
	}
	FString GErr;
	UEdGraph* Graph = UnrealAiBlueprintT3d::ResolveTargetGraph(BP, GraphName, GErr);
	if (!Graph)
	{
		return UnrealAiToolJson::Error(GErr);
	}

	TSet<UObject*> ExportSet;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (N)
		{
			ExportSet.Add(N);
		}
	}
	FString T3d;
	FEdGraphUtilities::ExportNodesToText(ExportSet, T3d);
	const bool bTruncated = T3d.Len() > MaxChars;
	if (bTruncated)
	{
		T3d.LeftInline(MaxChars);
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("graph_name"), Graph->GetName());
	O->SetStringField(TEXT("t3d_text"), T3d);
	O->SetNumberField(TEXT("t3d_chars"), static_cast<double>(T3d.Len()));
	O->SetBoolField(TEXT("truncated"), bTruncated);
	O->SetNumberField(TEXT("max_chars"), static_cast<double>(MaxChars));
	const FString Md = FString::Printf(
		TEXT("### blueprint_export_graph_t3d\n- Exported **%d** chars (`truncated=%s`).\n"),
		T3d.Len(),
		bTruncated ? TEXT("true") : TEXT("false"));
	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, Graph->GetName(), Md));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintT3dPreflightValidate(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	FString T3dIn;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required."));
	}
	if (!Args->TryGetStringField(TEXT("t3d_text"), T3dIn) || T3dIn.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("t3d_text is required."));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	UnrealAiNormalizeBlueprintObjectPath(Path);
	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(
			FString::Printf(TEXT("Could not load Blueprint at %s. Resolve path via asset search first."), *Path));
	}
	FString GErr;
	UEdGraph* Graph = UnrealAiBlueprintT3d::ResolveTargetGraph(BP, GraphName, GErr);
	if (!Graph)
	{
		return UnrealAiToolJson::Error(GErr);
	}
	FString Resolved = T3dIn;
	FString PErr;
	if (!UnrealAiBlueprintT3d::CollectAndResolvePlaceholders(Resolved, PErr))
	{
		return UnrealAiToolJson::Error(PErr);
	}
	const bool bCan = FEdGraphUtilities::CanImportNodesFromText(Graph, Resolved);
	if (!bCan)
	{
		FString Msg = TEXT("CanImportNodesFromText returned false for this graph + resolved T3D. Fix payload or graph target.");
		Msg += UnrealAiBlueprintT3d::DescribeT3dImportBlockerHint(Graph, BP);
		return UnrealAiToolJson::Error(Msg);
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetBoolField(TEXT("can_import"), true);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("graph_name"), Graph->GetName());
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGraphImportT3d(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	FString T3dIn;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required."));
	}
	if (!Args->TryGetStringField(TEXT("t3d_text"), T3dIn) || T3dIn.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("t3d_text is required."));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	if (!UnrealAiBlueprintTools_IsGameWritableBlueprintPath(Path))
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path must be a writable /Game Blueprint path."));
	}
	UnrealAiNormalizeBlueprintObjectPath(Path);
	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(
			FString::Printf(TEXT("Could not load Blueprint at %s. Resolve path via asset search first."), *Path));
	}
	FString GErr;
	UEdGraph* Graph = UnrealAiBlueprintT3d::ResolveTargetGraph(BP, GraphName, GErr);
	if (!Graph)
	{
		return UnrealAiToolJson::Error(GErr);
	}
	FString Resolved = T3dIn;
	FString PErr;
	if (!UnrealAiBlueprintT3d::CollectAndResolvePlaceholders(Resolved, PErr))
	{
		return UnrealAiToolJson::Error(PErr);
	}
	if (!FEdGraphUtilities::CanImportNodesFromText(Graph, Resolved))
	{
		FString Msg = TEXT("Preflight failed: CanImportNodesFromText is false. Run blueprint_t3d_preflight_validate and fix T3D.");
		Msg += UnrealAiBlueprintT3d::DescribeT3dImportBlockerHint(Graph, BP);
		return UnrealAiToolJson::Error(Msg);
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnBpT3dImport", "Unreal AI: blueprint_graph_import_t3d"));
	BP->Modify();
	Graph->Modify();
	TSet<UEdGraphNode*> Imported;
	FEdGraphUtilities::ImportNodesFromText(Graph, Resolved, Imported);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("graph_name"), Graph->GetName());
	O->SetNumberField(TEXT("imported_node_count"), static_cast<double>(Imported.Num()));
	const FString Md = FString::Printf(
		TEXT("### blueprint_graph_import_t3d\n- Imported nodes: **%d**\n"),
		Imported.Num());
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
	UBlueprint* BP = UnrealAiBlueprintTools_LoadBlueprintGame(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(
			FString::Printf(TEXT("Could not load Blueprint at %s. Resolve path via asset search first."), *Path));
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

	auto AddIssue = [&Issues](const FString& Code, const FString& Msg)
	{
		TSharedPtr<FJsonObject> I = MakeShared<FJsonObject>();
		I->SetStringField(TEXT("code"), Code);
		I->SetStringField(TEXT("message"), Msg);
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
							AddIssue(TEXT("null_linked_pin"), FString::Printf(TEXT("Pin %s on node %s has null link entry."), *Pin->PinName.ToString(), *LexToString(N->NodeGuid)));
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
										*Pin->PinName.ToString()));
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
							*LexToString(K2->NodeGuid)));
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
							*N->GetName()));
				}
				else
				{
					ByGuid.Add(N->NodeGuid, N);
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
