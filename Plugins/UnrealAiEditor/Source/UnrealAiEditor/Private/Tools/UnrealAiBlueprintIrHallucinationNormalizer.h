#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Blueprint IR hallucination normalizer.
 *
 * Purpose:
 * - keep hallucination-specific rewrites out of the main dispatch file
 * - provide one place to add/remove common model mistake mappings over time
 */
namespace UnrealAiBlueprintIrHallucinationNormalizer
{
	/**
	 * Normalizes a single IR node in-place when it matches a known hallucinated shape.
	 *
	 * @return true when any rewrite was applied.
	 */
	bool NormalizeNode(
		const TSharedPtr<FJsonObject>& Node,
		TArray<FString>& OutNotes,
		TArray<FString>& OutDeprecatedFields);
}

