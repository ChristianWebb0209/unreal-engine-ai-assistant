#pragma once

#include "CoreMinimal.h"

struct FUnrealAiEmbeddingRequest
{
	FString InputText;
	/** When true (indexer only), HTTP + wait run on the caller thread with HttpManager polling — never marshals to the game thread. */
	bool bBackgroundIndexer = false;
};

struct FUnrealAiEmbeddingResponse
{
	TArray<float> Vector;
	FString Error;
};

class IUnrealAiEmbeddingProvider
{
public:
	virtual ~IUnrealAiEmbeddingProvider() = default;

	virtual bool EmbedOne(
		const FString& ModelId,
		const FUnrealAiEmbeddingRequest& Request,
		FUnrealAiEmbeddingResponse& OutResponse) = 0;
};
