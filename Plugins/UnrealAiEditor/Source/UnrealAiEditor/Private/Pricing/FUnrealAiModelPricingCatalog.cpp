#include "Pricing/FUnrealAiModelPricingCatalog.h"

namespace UnrealAiPricingCatalogUtil
{
	static FString NormalizedSuffix(const FString& Key)
	{
		int32 Slash = INDEX_NONE;
		if (Key.FindLastChar(TEXT('/'), Slash))
		{
			return Key.Mid(Slash + 1);
		}
		return Key;
	}
}

void FUnrealAiModelPricingCatalog::EnsureLoaded()
{
	if (bLoaded)
	{
		return;
	}
	bLoaded = true;
	InputPerTokenByKey.Reset();
	OutputPerTokenByKey.Reset();
	SuffixToKeys.Reset();
	LowerKeyToCanonicalKey.Reset();

	// Curated built-in pricing catalog (approximate USD per 1M tokens).
	struct FSeed
	{
		const TCHAR* Id;
		double InPer1M;
		double OutPer1M;
	};
	static const FSeed Seeds[] = {
		{ TEXT("openai/gpt-5"), 5.00, 15.00 },
		{ TEXT("openai/gpt-5-mini"), 0.25, 2.00 },
		{ TEXT("openai/gpt-4o"), 5.00, 15.00 },
		{ TEXT("openai/gpt-4o-mini"), 0.15, 0.60 },
		{ TEXT("gpt-5"), 5.00, 15.00 },
		{ TEXT("gpt-5-mini"), 0.25, 2.00 },
		{ TEXT("gpt-4o"), 5.00, 15.00 },
		{ TEXT("gpt-4o-mini"), 0.15, 0.60 },
		{ TEXT("claude-3-7-sonnet-latest"), 3.00, 15.00 },
		{ TEXT("claude-3-5-sonnet-latest"), 3.00, 15.00 },
		{ TEXT("deepseek-chat"), 0.27, 1.10 },
		{ TEXT("deepseek-reasoner"), 0.55, 2.19 }
	};
	for (const FSeed& S : Seeds)
	{
		const FString Id = S.Id;
		InputPerTokenByKey.Add(Id, S.InPer1M / 1.0e6);
		OutputPerTokenByKey.Add(Id, S.OutPer1M / 1.0e6);
		LowerKeyToCanonicalKey.Add(Id.ToLower(), Id);
	}
	BuildSuffixIndex();
}

void FUnrealAiModelPricingCatalog::BuildSuffixIndex()
{
	SuffixToKeys.Reset();
	for (const TPair<FString, double>& P : InputPerTokenByKey)
	{
		const FString Suf = UnrealAiPricingCatalogUtil::NormalizedSuffix(P.Key);
		if (!Suf.IsEmpty())
		{
			SuffixToKeys.FindOrAdd(Suf.ToLower()).AddUnique(P.Key);
		}
	}
}

bool FUnrealAiModelPricingCatalog::FindCanonicalKey(const FString& ModelId, FString& OutKey) const
{
	if (ModelId.IsEmpty())
	{
		return false;
	}
	if (InputPerTokenByKey.Contains(ModelId))
	{
		OutKey = ModelId;
		return true;
	}
	if (const FString* Found = LowerKeyToCanonicalKey.Find(ModelId.ToLower()))
	{
		OutKey = *Found;
		return true;
	}
	const FString Suf = UnrealAiPricingCatalogUtil::NormalizedSuffix(ModelId).ToLower();
	if (const TArray<FString>* Arr = SuffixToKeys.Find(Suf))
	{
		if (Arr->Num() == 1)
		{
			OutKey = (*Arr)[0];
			return true;
		}
		// Prefer keys that share a longer prefix with the requested id (e.g. provider match).
		FString Best;
		int32 BestScore = -1;
		for (const FString& K : *Arr)
		{
			int32 Score = 0;
			const int32 MinLen = FMath::Min(K.Len(), ModelId.Len());
			for (int32 i = 0; i < MinLen; ++i)
			{
				if (K[i] == ModelId[i])
				{
					++Score;
				}
				else
				{
					break;
				}
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = K;
			}
		}
		if (!Best.IsEmpty())
		{
			OutKey = Best;
			return true;
		}
	}
	return false;
}

bool FUnrealAiModelPricingCatalog::TryEstimateUsd(const FString& ModelIdForApi, int64 PromptTokens, int64 CompletionTokens, double& OutUsd) const
{
	const_cast<FUnrealAiModelPricingCatalog*>(this)->EnsureLoaded();
	OutUsd = 0.0;
	FString Canon;
	if (!FindCanonicalKey(ModelIdForApi, Canon))
	{
		return false;
	}
	const double* InPtr = InputPerTokenByKey.Find(Canon);
	const double* OutPtr = OutputPerTokenByKey.Find(Canon);
	if (!InPtr && !OutPtr)
	{
		return false;
	}
	const double In = InPtr ? *InPtr : 0.0;
	const double Out = OutPtr ? *OutPtr : 0.0;
	OutUsd = static_cast<double>(PromptTokens) * In + static_cast<double>(CompletionTokens) * Out;
	return true;
}

void FUnrealAiModelPricingCatalog::GetRoughPriceHintLines(const FString& ModelIdForApi, TArray<FString>& OutLines) const
{
	OutLines.Reset();
	const_cast<FUnrealAiModelPricingCatalog*>(this)->EnsureLoaded();
	FString Canon;
	if (!FindCanonicalKey(ModelIdForApi, Canon))
	{
		return;
	}
	const double* InPtr = InputPerTokenByKey.Find(Canon);
	const double* OutPtr = OutputPerTokenByKey.Find(Canon);
	if (!InPtr && !OutPtr)
	{
		return;
	}
	const double In = InPtr ? *InPtr : 0.0;
	const double Out = OutPtr ? *OutPtr : 0.0;
	const double Per1MIn = In * 1.0e6;
	const double Per1MOut = Out * 1.0e6;
	OutLines.Add(FString::Printf(TEXT("Catalog match: %s"), *Canon));
	OutLines.Add(FString::Printf(TEXT("~$%.4f / 1M input tok, ~$%.4f / 1M output tok (Litellm dataset, approximate)"), Per1MIn, Per1MOut));
}
