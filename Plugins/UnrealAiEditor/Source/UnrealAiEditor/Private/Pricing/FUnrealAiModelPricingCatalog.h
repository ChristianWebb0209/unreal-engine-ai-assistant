#pragma once

#include "CoreMinimal.h"

/** Loads bundled Litellm-style model_prices JSON for rough USD estimates (best-effort). */
class FUnrealAiModelPricingCatalog
{
public:
	void EnsureLoaded();

	/** Returns false when the model is unknown or has no usable per-token pricing in the catalog. */
	bool TryEstimateUsd(const FString& ModelIdForApi, int64 PromptTokens, int64 CompletionTokens, double& OutUsd) const;

	/** Best-effort lookup for UI hints (may be empty). */
	void GetRoughPriceHintLines(const FString& ModelIdForApi, TArray<FString>& OutLines) const;

private:
	void LoadFromFile(const FString& AbsolutePath);
	void BuildSuffixIndex();

	mutable bool bLoaded = false;
	TMap<FString, double> InputPerTokenByKey;
	TMap<FString, double> OutputPerTokenByKey;
	TMap<FString, TArray<FString>> SuffixToKeys;
	TMap<FString, FString> LowerKeyToCanonicalKey;

	bool FindCanonicalKey(const FString& ModelId, FString& OutKey) const;
};
