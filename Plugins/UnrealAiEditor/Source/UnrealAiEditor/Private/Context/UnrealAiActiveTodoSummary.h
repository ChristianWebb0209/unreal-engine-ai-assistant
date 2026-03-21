#pragma once

#include "CoreMinimal.h"

/** One-line summary of persisted active todo plan for prompt tokens. */
FString UnrealAiFormatActiveTodoSummary(const FString& PlanJson, const TArray<bool>& StepsDone);
