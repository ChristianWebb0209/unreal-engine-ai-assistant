#include "Planning/UnrealAiStructuredPlanSummary.h"

#include "Dom/JsonObject.h"
#include "Planning/UnrealAiPlanDag.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString UnrealAiFormatActiveTodoSummary(const FString& PlanJson, const TArray<bool>& StepsDone)
{
	if (PlanJson.IsEmpty())
	{
		return FString();
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(PlanJson);
	if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
	{
		return FString::Printf(TEXT("(active plan present, %d chars)"), PlanJson.Len());
	}
	FString Title;
	Root->TryGetStringField(TEXT("title"), Title);
	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	int32 N = 0;
	if (Root->TryGetArrayField(TEXT("steps"), Steps) && Steps)
	{
		N = Steps->Num();
	}
	int32 Done = 0;
	for (int32 i = 0; i < StepsDone.Num() && i < N; ++i)
	{
		if (StepsDone[i])
		{
			++Done;
		}
	}
	const FString T = Title.IsEmpty() ? TEXT("Plan") : Title;
	return FString::Printf(TEXT("Active todo plan: \"%s\" — %d steps (%d marked done in UI)."), *T, N, Done);
}

FString UnrealAiFormatActivePlanDagSummary(const FString& DagJson, const TMap<FString, FString>& NodeStatusById)
{
	if (DagJson.IsEmpty())
	{
		return FString();
	}
	FUnrealAiPlanDag Dag;
	FString Err;
	if (!UnrealAiPlanDag::ParseDagJson(DagJson, Dag, Err))
	{
		return FString::Printf(TEXT("Active plan DAG present (%d chars); parse error: %s"), DagJson.Len(), *Err);
	}
	FString Title = Dag.Title.IsEmpty() ? TEXT("Plan DAG") : Dag.Title;
	FString Lines = FString::Printf(TEXT("\"%s\" — %d nodes: "), *Title, Dag.Nodes.Num());
	for (int32 i = 0; i < Dag.Nodes.Num(); ++i)
	{
		const FUnrealAiDagNode& N = Dag.Nodes[i];
		const FString* St = NodeStatusById.Find(N.Id);
		const FString Status = St && !St->IsEmpty() ? *St : TEXT("pending");
		if (i > 0)
		{
			Lines += TEXT("; ");
		}
		Lines += FString::Printf(TEXT("%s (%s): %s"), *N.Id, *Status, N.Title.IsEmpty() ? *N.Id : *N.Title);
	}
	return Lines;
}
