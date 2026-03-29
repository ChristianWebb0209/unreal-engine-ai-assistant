#pragma once

#include "CoreMinimal.h"
#include "Prompt/UnrealAiPromptAssembleParams.h"

namespace UnrealAiPromptBuilder
{
	/** Loads `prompts/chunks/*.md` via `FUnrealAiLinearPromptAssemblyStrategy`. */
	FString BuildSystemDeveloperContent(const FUnrealAiPromptAssembleParams& Params);
}
