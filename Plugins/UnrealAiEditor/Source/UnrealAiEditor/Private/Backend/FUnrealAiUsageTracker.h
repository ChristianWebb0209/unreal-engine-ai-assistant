#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

class IUnrealAiPersistence;
class FUnrealAiModelProfileRegistry;
class FUnrealAiModelPricingCatalog;

/** Persists cumulative per–model-profile token counts; USD is derived with the pricing catalog when possible. */
class FUnrealAiUsageTracker
{
public:
	FUnrealAiUsageTracker(IUnrealAiPersistence* InPersistence, FUnrealAiModelPricingCatalog* InPricing);

	void ReloadFromDisk();
	void RecordUsage(const FString& ModelProfileId, const FUnrealAiTokenUsage& Usage, const FUnrealAiModelProfileRegistry& Profiles);

	void AccumulateTotals(const FUnrealAiModelProfileRegistry& Profiles, int64& OutPrompt, int64& OutCompletion, double& OutUsdEstimate) const;

	void PerModelSummary(
		const FString& ModelProfileId,
		const FUnrealAiModelProfileRegistry& Profiles,
		FString& OutSummary,
		bool& bOutHasPricePiece) const;

private:
	void Save();
	FString ModelIdForPricing(const FString& ModelProfileId, const FUnrealAiModelProfileRegistry& Profiles) const;

	IUnrealAiPersistence* Persistence = nullptr;
	FUnrealAiModelPricingCatalog* Pricing = nullptr;

	struct FAccum
	{
		int64 Prompt = 0;
		int64 Completion = 0;
	};
	TMap<FString, FAccum> ByProfileId;
};
