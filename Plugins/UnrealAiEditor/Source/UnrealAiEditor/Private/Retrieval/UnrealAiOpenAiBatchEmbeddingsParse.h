#pragma once

#include "CoreMinimal.h"

namespace UnrealAiOpenAiBatchEmbeddingsParse
{
	/** Parse OpenAI-style embeddings JSON: sort `data[]` by `index`, fill `ExpectedCount` vectors. */
	bool ParseEmbeddingsDataFromResponse(
		const FString& ResponseBody,
		int32 ExpectedCount,
		TArray<TArray<float>>& OutVectors,
		FString& OutError);
}
