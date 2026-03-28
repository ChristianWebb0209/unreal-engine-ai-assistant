#pragma once

#include "CoreMinimal.h"

enum class EUnrealAiToolQueryShape : uint8
{
	Raw,
	Heuristic,
	Hybrid
};

/** §2.10 — lightweight verb/object hints; hybrid with raw query for safety. */
namespace UnrealAiToolQueryShaper
{
	void ShapeForRetrieval(const FString& RawUserText, FString& OutShaped, EUnrealAiToolQueryShape& OutShapeUsed);
	void BuildHybridQuery(const FString& RawUserText, const FString& Shaped, FString& OutHybrid);
}
