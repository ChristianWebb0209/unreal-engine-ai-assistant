#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"

/** One-line summary of persisted active todo plan (`unreal_ai.todo_plan`) for prompt tokens. */
FString UnrealAiFormatActiveTodoSummary(const FString& PlanJson, const TArray<bool>& StepsDone);

/** Compact summary of persisted Plan-mode DAG (`unreal_ai.plan_dag`) plus per-node status from context. */
FString UnrealAiFormatActivePlanDagSummary(const FString& DagJson, const TMap<FString, FString>& NodeStatusById);
