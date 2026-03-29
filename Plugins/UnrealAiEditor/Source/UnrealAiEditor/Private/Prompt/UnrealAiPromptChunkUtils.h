#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"
#include "Prompt/UnrealAiPromptAssembleParams.h"

namespace UnrealAiPromptChunkUtils
{
	/** Resolves `.../Plugins/UnrealAiEditor/prompts/<Subdir>` (e.g. `chunks`). */
	FString ResolvePromptSubdir(const TCHAR* Subdir);

	bool LoadChunk(const TCHAR* Subdir, const TCHAR* FileName, FString& Out);

	FString ExtractOperatingModeSection(const FString& Chunk02, EUnrealAiAgentMode Mode);

	void ApplyTemplateTokens(
		FString& Doc,
		const FUnrealAiPromptAssembleParams& P,
		const FAgentContextBuildResult& B);
}
