#include "Context/UnrealAiActiveTodoSummary.h"

#include "Dom/JsonObject.h"
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
