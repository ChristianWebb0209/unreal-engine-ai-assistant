#pragma once

#include "CoreMinimal.h"

struct FUnrealAiAgentTurnRequest;
class FUnrealAiToolCatalog;

/**
 * Controls which tools appear in the tiered LLM tool index and which invocations the harness allows,
 * using catalog tools[].agent_surfaces (orthogonal to modes: ask/agent/plan).
 *
 * When Catalog is null, all tools pass (backward compatible).
 */
namespace UnrealAiBlueprintToolGate
{
	bool PassesToolSurfaceFilter(
		const FUnrealAiAgentTurnRequest& Request,
		const FString& ToolId,
		const FUnrealAiToolCatalog* CatalogOpt = nullptr);
}
