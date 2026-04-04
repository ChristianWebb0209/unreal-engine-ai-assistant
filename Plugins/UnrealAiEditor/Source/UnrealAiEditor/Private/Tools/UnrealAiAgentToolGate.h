#pragma once

#include "CoreMinimal.h"

struct FUnrealAiAgentTurnRequest;
class FUnrealAiToolCatalog;

/**
 * Tiered tool index + harness invoke filter using catalog tools[].agent_surfaces.
 * Selects MainAgent vs BlueprintBuilder vs EnvironmentBuilder from the active turn request.
 */
namespace UnrealAiAgentToolGate
{
	bool PassesToolSurfaceFilter(
		const FUnrealAiAgentTurnRequest& Request,
		const FString& ToolId,
		const FUnrealAiToolCatalog* CatalogOpt = nullptr);
}
