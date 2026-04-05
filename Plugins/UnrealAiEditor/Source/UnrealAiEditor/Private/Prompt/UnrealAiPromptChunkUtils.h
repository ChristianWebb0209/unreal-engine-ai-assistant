#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"
#include "Prompt/UnrealAiPromptAssembleParams.h"

namespace UnrealAiPromptChunkUtils
{
	/** Resolves `.../Plugins/UnrealAiEditor/prompts/<Subdir>` (e.g. `chunks` or `chunks/common`). */
	FString ResolvePromptSubdir(const TCHAR* Subdir);

	bool LoadChunk(const TCHAR* Subdir, const TCHAR* FileName, FString& Out);

	FString ExtractOperatingModeSection(const FString& Chunk02, EUnrealAiAgentMode Mode);

	void ApplyTemplateTokens(
		FString& Doc,
		const FUnrealAiPromptAssembleParams& P,
		const FAgentContextBuildResult& B);

	/**
	 * Cheap fingerprint of markdown under `prompts/chunks` (recursive). Recomputes at most every ~2.5s
	 * so callers can invalidate caches when prompt files change without stat storming every LLM round.
	 */
	uint32 GetPromptsLooseSignatureThrottled();
}
