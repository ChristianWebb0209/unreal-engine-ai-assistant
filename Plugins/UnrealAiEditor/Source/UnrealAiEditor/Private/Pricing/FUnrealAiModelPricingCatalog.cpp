#include "Pricing/FUnrealAiModelPricingCatalog.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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

	TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(TEXT("UnrealAiEditor"));
	if (!P.IsValid())
	{
		return;
	}
	const FString Path = FPaths::Combine(P->GetBaseDir(), TEXT("Resources/ModelPricing/model_prices_and_context_window.json"));
	LoadFromFile(FPaths::ConvertRelativePathToFull(Path));
	BuildSuffixIndex();
}

void FUnrealAiModelPricingCatalog::LoadFromFile(const FString& AbsolutePath)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *AbsolutePath))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
	{
		const FString& Key = Pair.Key;
		if (Key == TEXT("sample_spec"))
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Obj) || !Obj->IsValid())
		{
			continue;
		}
		double InCost = 0.0;
		double OutCost = 0.0;
		const bool bHasIn = (*Obj)->TryGetNumberField(TEXT("input_cost_per_token"), InCost);
		const bool bHasOut = (*Obj)->TryGetNumberField(TEXT("output_cost_per_token"), OutCost);
		if (!bHasIn && !bHasOut)
		{
			continue;
		}
		InputPerTokenByKey.Add(Key, InCost);
		OutputPerTokenByKey.Add(Key, OutCost);
		LowerKeyToCanonicalKey.Add(Key.ToLower(), Key);
	}
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
