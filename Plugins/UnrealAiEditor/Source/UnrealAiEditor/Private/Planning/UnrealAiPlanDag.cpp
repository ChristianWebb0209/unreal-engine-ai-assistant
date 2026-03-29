#include "Planning/UnrealAiPlanDag.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAiPlanDag
{
	static void ParseDependsArray(const FJsonObject& O, TArray<FString>& OutDeps)
	{
		const TArray<TSharedPtr<FJsonValue>>* Deps = nullptr;
		if (!O.TryGetArrayField(TEXT("depends_on"), Deps) || !Deps)
		{
			O.TryGetArrayField(TEXT("dependsOn"), Deps);
		}
		if (!Deps)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& D : *Deps)
		{
			if (D.IsValid() && D->Type == EJson::String)
			{
				const FString Dep = D->AsString().TrimStartAndEnd();
				if (!Dep.IsEmpty())
				{
					OutDeps.Add(Dep);
				}
			}
		}
	}

	static bool ParseOneDagNodeObject(const TSharedPtr<FJsonObject>& O, FUnrealAiDagNode& OutNode)
	{
		if (!O.IsValid())
		{
			return false;
		}
		O->TryGetStringField(TEXT("id"), OutNode.Id);
		O->TryGetStringField(TEXT("title"), OutNode.Title);
		if (OutNode.Title.IsEmpty())
		{
			O->TryGetStringField(TEXT("name"), OutNode.Title);
		}
		O->TryGetStringField(TEXT("hint"), OutNode.Hint);
		if (OutNode.Hint.IsEmpty())
		{
			O->TryGetStringField(TEXT("detail"), OutNode.Hint);
		}
		ParseDependsArray(*O, OutNode.DependsOn);
		return !OutNode.Id.IsEmpty();
	}

	static void AppendNodesFromJsonArray(const TArray<TSharedPtr<FJsonValue>>& Arr, TArray<FUnrealAiDagNode>& InOutNodes)
	{
		for (const TSharedPtr<FJsonValue>& V : Arr)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			FUnrealAiDagNode N;
			if (ParseOneDagNodeObject(O, N))
			{
				InOutNodes.Add(MoveTemp(N));
			}
		}
	}

	static bool IsDoneLike(const FString& S)
	{
		return S == TEXT("success") || S == TEXT("failed") || S == TEXT("skipped");
	}

	bool IsTerminalStatus(const FString& Status)
	{
		return IsDoneLike(Status);
	}

	bool IsSuccessfulStatus(const FString& Status)
	{
		return Status == TEXT("success") || Status == TEXT("skipped");
	}

	bool ParseDagJson(const FString& DagJson, FUnrealAiPlanDag& OutDag, FString& OutError)
	{
		OutDag = FUnrealAiPlanDag();
		OutError.Empty();

		FString NormalizedDagJson = DagJson.TrimStartAndEnd();
		{
			const int32 TagStart = NormalizedDagJson.Find(TEXT("<chat-name"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			if (TagStart > 0)
			{
				NormalizedDagJson = NormalizedDagJson.Left(TagStart).TrimEnd();
			}
		}

		auto TryParseObject = [](const FString& Candidate, TSharedPtr<FJsonObject>& InOutRoot) -> bool
		{
			InOutRoot.Reset();
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Candidate);
			if (!FJsonSerializer::Deserialize(Reader, InOutRoot) || !InOutRoot.IsValid())
			{
				return false;
			}
			return true;
		};

		TSharedPtr<FJsonObject> Root;
		FString FenceStripped;
		{
			if (TryParseObject(NormalizedDagJson, Root))
			{
				goto ParsedOk;
			}
		}

		FenceStripped = NormalizedDagJson;
		FenceStripped = FenceStripped.Replace(TEXT("```json"), TEXT(""), ESearchCase::IgnoreCase);
		FenceStripped = FenceStripped.Replace(TEXT("```"), TEXT(""), ESearchCase::IgnoreCase);
		FenceStripped = FenceStripped.TrimStartAndEnd();
		if (TryParseObject(FenceStripped, Root))
		{
			goto ParsedOk;
		}

		{
			const int32 FirstBrace = FenceStripped.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart);
			const int32 LastBrace = FenceStripped.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (FirstBrace >= 0 && LastBrace > FirstBrace)
			{
				const FString Sub = FenceStripped.Mid(FirstBrace, (LastBrace - FirstBrace) + 1);
				if (TryParseObject(Sub, Root))
				{
					goto ParsedOk;
				}
			}
		}

		OutError = TEXT("Planner output is not valid JSON.");
		return false;

	ParsedOk:
		FString Schema;
		Root->TryGetStringField(TEXT("schema"), Schema);
		if (!Schema.IsEmpty() && Schema != TEXT("unreal_ai.plan_dag"))
		{
			OutError = FString::Printf(TEXT("Unexpected DAG schema: %s"), *Schema);
			return false;
		}
		Root->TryGetStringField(TEXT("title"), OutDag.Title);
		if (OutDag.Title.IsEmpty())
		{
			Root->TryGetStringField(TEXT("definitionOfDone"), OutDag.Title);
		}

		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (Root->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes && Nodes->Num() > 0)
		{
			OutDag.Nodes.Reserve(Nodes->Num());
			AppendNodesFromJsonArray(*Nodes, OutDag.Nodes);
		}
		// Legacy `steps[]` (same shape as nodes) — prefer `nodes[]` in new prompts; removal tracked for a future release.
		if (OutDag.Nodes.Num() == 0)
		{
			const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
			if (Root->TryGetArrayField(TEXT("steps"), Steps) && Steps && Steps->Num() > 0)
			{
				OutDag.Nodes.Reserve(Steps->Num());
				AppendNodesFromJsonArray(*Steps, OutDag.Nodes);
			}
		}
		if (OutDag.Nodes.Num() == 0)
		{
			OutError = TEXT(
				"DAG must contain a non-empty `nodes` array (canonical) or legacy `steps` array with `id`/`dependsOn`.");
			return false;
		}
		return true;
	}

	bool ValidateDag(const FUnrealAiPlanDag& Dag, int32 MaxNodes, FString& OutError)
	{
		OutError.Empty();
		if (Dag.Nodes.Num() <= 0)
		{
			OutError = TEXT("DAG has no nodes.");
			return false;
		}
		if (Dag.Nodes.Num() > FMath::Max(1, MaxNodes))
		{
			OutError = FString::Printf(TEXT("DAG exceeds max nodes (%d > %d)."), Dag.Nodes.Num(), MaxNodes);
			return false;
		}

		TMap<FString, int32> IndexById;
		for (int32 i = 0; i < Dag.Nodes.Num(); ++i)
		{
			if (IndexById.Contains(Dag.Nodes[i].Id))
			{
				OutError = FString::Printf(TEXT("Duplicate DAG node id: %s"), *Dag.Nodes[i].Id);
				return false;
			}
			IndexById.Add(Dag.Nodes[i].Id, i);
		}
		for (const FUnrealAiDagNode& N : Dag.Nodes)
		{
			for (const FString& D : N.DependsOn)
			{
				if (!IndexById.Contains(D))
				{
					OutError = FString::Printf(TEXT("Node '%s' depends on unknown node '%s'."), *N.Id, *D);
					return false;
				}
				if (D == N.Id)
				{
					OutError = FString::Printf(TEXT("Node '%s' cannot depend on itself."), *N.Id);
					return false;
				}
			}
		}

		TMap<FString, int32> State;
		TFunction<bool(const FString&)> Dfs;
		Dfs = [&](const FString& Id) -> bool
		{
			const int32* S = State.Find(Id);
			const int32 Cur = S ? *S : 0;
			if (Cur == 1)
			{
				return false;
			}
			if (Cur == 2)
			{
				return true;
			}
			State.Add(Id, 1);
			const int32* I = IndexById.Find(Id);
			if (!I || !Dag.Nodes.IsValidIndex(*I))
			{
				return false;
			}
			for (const FString& Dep : Dag.Nodes[*I].DependsOn)
			{
				if (!Dfs(Dep))
				{
					return false;
				}
			}
			State.Add(Id, 2);
			return true;
		};
		for (const FUnrealAiDagNode& N : Dag.Nodes)
		{
			if (!Dfs(N.Id))
			{
				OutError = TEXT("DAG contains a cycle.");
				return false;
			}
		}
		return true;
	}

	void GetReadyNodeIds(
		const FUnrealAiPlanDag& Dag,
		const TMap<FString, FString>& NodeStatusById,
		TArray<FString>& OutNodeIds)
	{
		OutNodeIds.Reset();
		for (const FUnrealAiDagNode& N : Dag.Nodes)
		{
			if (const FString* Status = NodeStatusById.Find(N.Id))
			{
				if (IsDoneLike(*Status) || *Status == TEXT("running"))
				{
					continue;
				}
			}
			bool bAllDepsSatisfied = true;
			for (const FString& Dep : N.DependsOn)
			{
				const FString* DepStatus = NodeStatusById.Find(Dep);
				if (!DepStatus || !IsSuccessfulStatus(*DepStatus))
				{
					bAllDepsSatisfied = false;
					break;
				}
			}
			if (bAllDepsSatisfied)
			{
				OutNodeIds.Add(N.Id);
			}
		}
	}

	void CollectTransitiveDependents(const FUnrealAiPlanDag& Dag, const FString& FailedNodeId, TArray<FString>& OutDependents)
	{
		OutDependents.Reset();
		if (FailedNodeId.IsEmpty())
		{
			return;
		}
		TMap<FString, TArray<FString>> DependentsOf;
		for (const FUnrealAiDagNode& N : Dag.Nodes)
		{
			for (const FString& Dep : N.DependsOn)
			{
				DependentsOf.FindOrAdd(Dep).Add(N.Id);
			}
		}
		TSet<FString> Visited;
		Visited.Add(FailedNodeId);
		TArray<FString> Queue;
		Queue.Add(FailedNodeId);
		while (Queue.Num() > 0)
		{
			const FString U = Queue[0];
			Queue.RemoveAt(0);
			if (const TArray<FString>* Next = DependentsOf.Find(U))
			{
				for (const FString& V : *Next)
				{
					if (!Visited.Contains(V))
					{
						Visited.Add(V);
						OutDependents.Add(V);
						Queue.Add(V);
					}
				}
			}
		}
	}

	void ComputeParallelWaves(const FUnrealAiPlanDag& Dag, TArray<TArray<FString>>& OutWaves)
	{
		OutWaves.Reset();
		if (Dag.Nodes.Num() == 0)
		{
			return;
		}
		TSet<FString> Assigned;
		Assigned.Reserve(Dag.Nodes.Num());
		while (Assigned.Num() < Dag.Nodes.Num())
		{
			TArray<FString> Wave;
			for (const FUnrealAiDagNode& N : Dag.Nodes)
			{
				if (Assigned.Contains(N.Id))
				{
					continue;
				}
				bool bAllDepsPlaced = true;
				for (const FString& Dep : N.DependsOn)
				{
					if (!Assigned.Contains(Dep))
					{
						bAllDepsPlaced = false;
						break;
					}
				}
				if (bAllDepsPlaced)
				{
					Wave.Add(N.Id);
				}
			}
			if (Wave.Num() == 0)
			{
				break;
			}
			for (const FString& Id : Wave)
			{
				Assigned.Add(Id);
			}
			OutWaves.Add(MoveTemp(Wave));
		}
	}

	bool SerializeDagJson(const FUnrealAiPlanDag& Dag, FString& OutJson, FString& OutError)
	{
		OutJson.Empty();
		OutError.Empty();
		if (Dag.Nodes.Num() == 0)
		{
			OutError = TEXT("DAG has no nodes to serialize.");
			return false;
		}
		const TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("unreal_ai.plan_dag"));
		if (!Dag.Title.IsEmpty())
		{
			Root->SetStringField(TEXT("title"), Dag.Title);
		}
		TArray<TSharedPtr<FJsonValue>> NodesArr;
		NodesArr.Reserve(Dag.Nodes.Num());
		for (const FUnrealAiDagNode& N : Dag.Nodes)
		{
			const TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("id"), N.Id);
			if (!N.Title.IsEmpty())
			{
				O->SetStringField(TEXT("title"), N.Title);
			}
			if (!N.Hint.IsEmpty())
			{
				O->SetStringField(TEXT("hint"), N.Hint);
			}
			if (N.DependsOn.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Deps;
				Deps.Reserve(N.DependsOn.Num());
				for (const FString& D : N.DependsOn)
				{
					Deps.Add(MakeShared<FJsonValueString>(D));
				}
				O->SetArrayField(TEXT("dependsOn"), Deps);
			}
			NodesArr.Add(MakeShared<FJsonValueObject>(O.ToSharedRef()));
		}
		Root->SetArrayField(TEXT("nodes"), NodesArr);
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			OutError = TEXT("JSON serialization failed.");
			return false;
		}
		return true;
	}

	bool MergeReplanNewNodesOntoSuccesses(
		const FUnrealAiPlanDag& OldDag,
		const TMap<FString, FString>& NodeStatusById,
		const FUnrealAiPlanDag& NewDagFromPlanner,
		FUnrealAiPlanDag& OutMerged,
		TSet<FString>& OutFreshNodeIds,
		FString& OutError)
	{
		OutMerged = FUnrealAiPlanDag();
		OutFreshNodeIds.Reset();
		OutError.Empty();

		if (NewDagFromPlanner.Nodes.Num() == 0)
		{
			OutError = TEXT("Replan DAG must include at least one new node.");
			return false;
		}

		TSet<FString> SuccessIds;
		for (const TPair<FString, FString>& Pair : NodeStatusById)
		{
			if (Pair.Value.Equals(TEXT("success"), ESearchCase::IgnoreCase))
			{
				SuccessIds.Add(Pair.Key);
			}
		}

		TSet<FString> NewIds;
		for (const FUnrealAiDagNode& N : NewDagFromPlanner.Nodes)
		{
			NewIds.Add(N.Id);
		}

		for (const FUnrealAiDagNode& N : NewDagFromPlanner.Nodes)
		{
			if (SuccessIds.Contains(N.Id))
			{
				OutError = FString::Printf(
					TEXT("Replan node id '%s' collides with an already-completed node id; use new ids for new work."),
					*N.Id);
				return false;
			}
			for (const FString& Dep : N.DependsOn)
			{
				if (!SuccessIds.Contains(Dep) && !NewIds.Contains(Dep))
				{
					OutError = FString::Printf(
						TEXT("Replan node '%s' depends on '%s', which is not completed (success) and not in the replan node set."),
						*N.Id,
						*Dep);
					return false;
				}
			}
		}

		OutMerged.Title = !NewDagFromPlanner.Title.IsEmpty() ? NewDagFromPlanner.Title : OldDag.Title;
		OutMerged.Nodes.Reserve(SuccessIds.Num() + NewDagFromPlanner.Nodes.Num());

		for (const FUnrealAiDagNode& ON : OldDag.Nodes)
		{
			if (SuccessIds.Contains(ON.Id))
			{
				OutMerged.Nodes.Add(ON);
			}
		}
		for (const FUnrealAiDagNode& NN : NewDagFromPlanner.Nodes)
		{
			OutMerged.Nodes.Add(NN);
			OutFreshNodeIds.Add(NN.Id);
		}

		if (!ValidateDag(OutMerged, 64, OutError))
		{
			OutMerged = FUnrealAiPlanDag();
			OutFreshNodeIds.Reset();
			return false;
		}
		return true;
	}
}
