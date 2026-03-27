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

private:
	FUnrealAiModelProfileRegistry* Profiles = nullptr;
};
