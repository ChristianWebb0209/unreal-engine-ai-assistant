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
	static void ExtractFeatureRecords(TArray<FUnrealAiBlueprintFeatureRecord>& OutRecords);
};
