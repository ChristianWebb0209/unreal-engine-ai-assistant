#pragma once

#include "Retrieval/IUnrealAiEmbeddingProvider.h"

class FUnrealAiModelProfileRegistry;

class FOpenAiCompatibleEmbeddingProvider final : public IUnrealAiEmbeddingProvider
{
public:
	explicit FOpenAiCompatibleEmbeddingProvider(FUnrealAiModelProfileRegistry* InProfiles);

	virtual bool EmbedOne(
		const FString& ModelId,
		const FUnrealAiEmbeddingRequest& Request,
		FUnrealAiEmbeddingResponse& OutResponse) override;

	virtual bool EmbedBatch(
		const FString& ModelId,
		const TArray<FString>& InputTexts,
		bool bBackgroundIndexer,
		TArray<TArray<float>>& OutVectors,
		FString& OutError) override;

private:
	bool EmbedTexts_OnGameThread(
		const FString& ModelId,
		const TArray<FString>& InputTexts,
		TArray<TArray<float>>& OutVectors,
		FString& OutError,
		bool bCallerThreadPumpsHttp);

	FUnrealAiModelProfileRegistry* Profiles = nullptr;
};
