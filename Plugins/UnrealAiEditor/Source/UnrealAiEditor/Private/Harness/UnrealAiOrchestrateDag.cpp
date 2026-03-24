#include "Harness/UnrealAiOrchestrateDag.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAiOrchestrateDag
{
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

	bool ParseDagJson(const FString& DagJson, FUnrealAiOrchestrateDag& OutDag, FString& OutError)
	{
		OutDag = FUnrealAiOrchestrateDag();
		OutError.Empty();

		// Planner text is supposed to be "DAG-only JSON", but in practice many models wrap it in markdown
		// code fences (```json ... ```) or include leading/trailing prose. Be tolerant and try to extract
		// the first JSON object.
		auto TryParseObject = [&OutError](const FString& Candidate, TSharedPtr<FJsonObject>& InOutRoot) -> bool
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
			const FString Trimmed = DagJson.TrimStartAndEnd();
			if (TryParseObject(Trimmed, Root))
			{
				goto ParsedOk;
			}
		}

		// Strip code fences if present.
		FenceStripped = DagJson;
		FenceStripped = FenceStripped.Replace(TEXT("```json"), TEXT(""), ESearchCase::IgnoreCase);
		FenceStripped = FenceStripped.Replace(TEXT("```"), TEXT(""), ESearchCase::IgnoreCase);
		FenceStripped = FenceStripped.TrimStartAndEnd();
		if (TryParseObject(FenceStripped, Root))
		{
			goto ParsedOk;
		}

		// Fallback: find first '{' and last '}' and parse the substring.
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
		if (!Schema.IsEmpty() && Schema != TEXT("unreal_ai.orchestrate_dag"))
		{
			OutError = FString::Printf(TEXT("Unexpected DAG schema: %s"), *Schema);
			return false;
		}
		Root->TryGetStringField(TEXT("title"), OutDag.Title);

		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Root->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes || Nodes->Num() == 0)
		{
			OutError = TEXT("DAG must contain a non-empty nodes array.");
			return false;
		}
		OutDag.Nodes.Reserve(Nodes->Num());
		for (const TSharedPtr<FJsonValue>& V : *Nodes)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid())
			{
				continue;
			}
			FUnrealAiDagNode N;
			O->TryGetStringField(TEXT("id"), N.Id);
			O->TryGetStringField(TEXT("title"), N.Title);
			O->TryGetStringField(TEXT("hint"), N.Hint);
			const TArray<TSharedPtr<FJsonValue>>* Deps = nullptr;
			if (O->TryGetArrayField(TEXT("depends_on"), Deps) && Deps)
			{
				for (const TSharedPtr<FJsonValue>& D : *Deps)
				{
					if (D.IsValid() && D->Type == EJson::String)
					{
						const FString Dep = D->AsString().TrimStartAndEnd();
						if (!Dep.IsEmpty())
						{
							N.DependsOn.Add(Dep);
						}
					}
				}
			}
			if (!N.Id.IsEmpty())
			{
				OutDag.Nodes.Add(MoveTemp(N));
			}
		}
		if (OutDag.Nodes.Num() == 0)
		{
			OutError = TEXT("DAG nodes array has no valid node ids.");
			return false;
		}
		return true;
	}

	bool ValidateDag(const FUnrealAiOrchestrateDag& Dag, int32 MaxNodes, FString& OutError)
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

		TMap<FString, int32> State; // 0 unvisited, 1 visiting, 2 done
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
		const FUnrealAiOrchestrateDag& Dag,
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
}

