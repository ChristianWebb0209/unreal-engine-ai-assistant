#pragma once

#include "CoreMinimal.h"

struct FUnrealAiBlueprintFeatureRecord
{
	FString AssetPath;
	FString Text;
};

class FUnrealAiBlueprintFeatureExtractor
{
public:
	/** When MaxRecords <= 0, uses an internal default cap (4000). */
	static void ExtractFeatureRecords(TArray<FUnrealAiBlueprintFeatureRecord>& OutRecords, int32 MaxRecords = 0);
};
