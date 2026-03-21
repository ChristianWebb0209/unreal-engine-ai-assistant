#include "Backend/FUnrealAiUsageTracker.h"

#include "Backend/IUnrealAiPersistence.h"
#include "Dom/JsonObject.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Pricing/FUnrealAiModelPricingCatalog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FUnrealAiUsageTracker::FUnrealAiUsageTracker(IUnrealAiPersistence* InPersistence, FUnrealAiModelPricingCatalog* InPricing)
	: Persistence(InPersistence)
	, Pricing(InPricing)
{
	ReloadFromDisk();
}

void FUnrealAiUsageTracker::ReloadFromDisk()
{
	ByProfileId.Reset();
	if (!Persistence)
	{
		return;
	}
	FString Json;
	if (!Persistence->LoadUsageStatsJson(Json) || Json.IsEmpty())
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	const TSharedPtr<FJsonObject>* ModelsObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("models"), ModelsObj) || !ModelsObj->IsValid())
	{
		return;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ModelsObj)->Values)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(O) || !O->IsValid())
		{
			continue;
		}
		FAccum A;
		double P = 0, C = 0;
		if ((*O)->TryGetNumberField(TEXT("prompt"), P))
		{
			A.Prompt = static_cast<int64>(P);
		}
		if ((*O)->TryGetNumberField(TEXT("completion"), C))
		{
			A.Completion = static_cast<int64>(C);
		}
		ByProfileId.Add(Pair.Key, A);
	}
}

void FUnrealAiUsageTracker::Save()
{
	if (!Persistence)
	{
		return;
	}
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> Models = MakeShared<FJsonObject>();
	for (const TPair<FString, FAccum>& Pair : ByProfileId)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("prompt"), static_cast<double>(Pair.Value.Prompt));
		O->SetNumberField(TEXT("completion"), static_cast<double>(Pair.Value.Completion));
		Models->SetObjectField(Pair.Key, O);
	}
	Root->SetObjectField(TEXT("models"), Models);
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), W))
	{
		return;
	}
	Persistence->SaveUsageStatsJson(Out);
}

FString FUnrealAiUsageTracker::ModelIdForPricing(const FString& ModelProfileId, const FUnrealAiModelProfileRegistry& Profiles) const
{
	FUnrealAiModelCapabilities Cap;
	Profiles.GetEffectiveCapabilities(ModelProfileId, Cap);
	if (!Cap.ModelIdForApi.IsEmpty())
	{
		return Cap.ModelIdForApi;
	}
	return ModelProfileId;
}

void FUnrealAiUsageTracker::RecordUsage(
	const FString& ModelProfileId,
	const FUnrealAiTokenUsage& Usage,
	const FUnrealAiModelProfileRegistry& Profiles)
{
	if (ModelProfileId.IsEmpty() || (!Usage.PromptTokens && !Usage.CompletionTokens && !Usage.TotalTokens))
	{
		return;
	}
	int32 P = Usage.PromptTokens;
	int32 C = Usage.CompletionTokens;
	if (P == 0 && C == 0 && Usage.TotalTokens > 0)
	{
		// Provider only total — split conservatively for display (no price accuracy).
		P = Usage.TotalTokens / 2;
		C = Usage.TotalTokens - P;
	}
	FAccum& A = ByProfileId.FindOrAdd(ModelProfileId);
	A.Prompt += P;
	A.Completion += C;
	Save();
}

void FUnrealAiUsageTracker::AccumulateTotals(
	const FUnrealAiModelProfileRegistry& Profiles,
	int64& OutPrompt,
	int64& OutCompletion,
	double& OutUsdEstimate) const
{
	OutPrompt = 0;
	OutCompletion = 0;
	OutUsdEstimate = 0.0;
	if (!Pricing)
	{
		for (const TPair<FString, FAccum>& Pair : ByProfileId)
		{
			OutPrompt += Pair.Value.Prompt;
			OutCompletion += Pair.Value.Completion;
		}
		return;
	}
	Pricing->EnsureLoaded();
	for (const TPair<FString, FAccum>& Pair : ByProfileId)
	{
		OutPrompt += Pair.Value.Prompt;
		OutCompletion += Pair.Value.Completion;
		const FString Mid = ModelIdForPricing(Pair.Key, Profiles);
		double U = 0.0;
		if (Pricing->TryEstimateUsd(Mid, Pair.Value.Prompt, Pair.Value.Completion, U))
		{
			OutUsdEstimate += U;
		}
	}
}

void FUnrealAiUsageTracker::PerModelSummary(
	const FString& ModelProfileId,
	const FUnrealAiModelProfileRegistry& Profiles,
	FString& OutSummary,
	bool& bOutHasPricePiece) const
{
	bOutHasPricePiece = false;
	const FAccum* A = ByProfileId.Find(ModelProfileId);
	const int64 P = A ? A->Prompt : 0;
	const int64 C = A ? A->Completion : 0;
	const FString Mid = ModelIdForPricing(ModelProfileId, Profiles);
	double U = 0.0;
	if (Pricing)
	{
		Pricing->EnsureLoaded();
	}
	const bool bPrice = Pricing && Pricing->TryEstimateUsd(Mid, P, C, U);
	if (bPrice)
	{
		bOutHasPricePiece = true;
		OutSummary = FString::Printf(TEXT("in %lld tok · out %lld tok · ~$%.4f"), P, C, U);
	}
	else
	{
		OutSummary = FString::Printf(TEXT("in %lld tok · out %lld tok"), P, C);
	}
}
