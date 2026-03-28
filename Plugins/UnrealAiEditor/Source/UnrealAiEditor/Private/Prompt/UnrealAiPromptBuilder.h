#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"

/** Parameters for assembling static prompt chunks + transcript tokens (see `prompts/README.md`). */
struct FUnrealAiPromptAssembleParams
{
	const FAgentContextBuildResult* Built = nullptr;
	EUnrealAiAgentMode Mode = EUnrealAiAgentMode::Agent;
	int32 LlmRound = 1;
	int32 MaxLlmRounds = 32;
	FString ThreadId;
	bool bIncludeExecutionSubturnChunk = false;
	bool bIncludePlanDagChunk = false;
};

namespace UnrealAiPromptBuilder
{
	/** Loads `prompts/chunks/*.md`, applies mode slice + token substitution. */
	FString BuildSystemDeveloperContent(const FUnrealAiPromptAssembleParams& Params);
}
