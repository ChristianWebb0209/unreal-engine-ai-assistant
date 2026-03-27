#pragma once

#include "CoreMinimal.h"

struct FUnrealAiEmbeddingRequest
{
	FString InputText;
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
