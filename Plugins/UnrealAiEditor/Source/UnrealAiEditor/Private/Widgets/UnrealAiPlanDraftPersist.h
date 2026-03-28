#pragma once

#include "CoreMinimal.h"

namespace UnrealAiPlanDraftPersist
{
	FString WrapDraftFile(const FString& DagJson);
	/** Returns false if file is missing dagJson and is not valid raw DAG text. */
	bool TryUnwrapDraftFile(const FString& FileBody, FString& OutDagJson);
}
