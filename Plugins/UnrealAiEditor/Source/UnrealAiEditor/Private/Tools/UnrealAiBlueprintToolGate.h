#pragma once

#include "CoreMinimal.h"

struct FUnrealAiAgentTurnRequest;

/**
 * Controls which Blueprint graph-mutation tools appear in the LLM tool index.
 * Main Agent turns omit tools that are reserved for automated Blueprint Builder sub-turns.
 */
class FUnrealAiToolCatalog;

namespace UnrealAiBlueprintToolGate
{
	bool PassesToolSurfaceFilter(const FUnrealAiAgentTurnRequest& Request, const FString& ToolId);

	/**
	 * Blueprint Builder BM25 leg: narrow lexical corpus (core Blueprint tools + blueprints category + small utility allowlist).
	 * Composed with PassesToolSurfaceFilter in UnrealAiToolSurfacePipeline when Request.bBlueprintBuilderTurn.
	 */
	bool PassesBuilderNarrowBm25Corpus(const FUnrealAiToolCatalog& Catalog, const FString& ToolId);
}
