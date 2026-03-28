#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FAgentContextState;

/** §2.11 — domain tags from editor snapshot / recent UI (boost only, no hard exclusion). */
namespace UnrealAiToolContextBias
{
	void GatherActiveDomainTags(const FAgentContextState* State, TArray<FString>& OutTags);
	void CollectToolDomainTags(const TSharedPtr<FJsonObject>& ToolDef, TArray<FString>& OutTags);
	float ScoreMultiplierForTool(const TArray<FString>& ToolDomainTags, const TArray<FString>& ActiveTags);
}
