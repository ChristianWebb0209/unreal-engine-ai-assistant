#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

/** Fuzzy search tools (scene, asset index, project files). */
FUnrealAiToolInvocationResult UnrealAiDispatch_SceneFuzzySearch(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_AssetIndexFuzzySearch(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_SourceSearchFuzzy(const TSharedPtr<FJsonObject>& Args);
