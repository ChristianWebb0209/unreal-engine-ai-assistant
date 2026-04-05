#pragma once

#include "CoreMinimal.h"
#include "Prompt/UnrealAiPromptAssembleParams.h"

namespace UnrealAiPromptBuilder
{
	/** Loads `prompts/chunks/common/*.md` (and domain subtrees) via `FUnrealAiLinearPromptAssemblyStrategy`. */
	FString BuildSystemDeveloperContent(const FUnrealAiPromptAssembleParams& Params);
}
