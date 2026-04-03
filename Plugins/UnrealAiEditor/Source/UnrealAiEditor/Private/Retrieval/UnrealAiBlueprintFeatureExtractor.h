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
	/**
	 * Blueprint metadata from the asset registry (/Game by default).
	 * @param MaxRecords When <= 0, uses an internal default cap (~800).
	 * @param bIncludeEngineBlueprints When true, also indexes /Engine Blueprints (can be very large).
	 */
	static void ExtractFeatureRecords(
		TArray<FUnrealAiBlueprintFeatureRecord>& OutRecords,
		int32 MaxRecords,
		bool bIncludeEngineBlueprints);
};
