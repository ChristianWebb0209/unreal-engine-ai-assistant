#pragma once

#include "CoreMinimal.h"

class FUnrealAiToolCatalog;
class IAgentContextService;
struct FUnrealAiAgentTurnRequest;
struct FUnrealAiModelCapabilities;
struct FUnrealAiToolPackOptions;
struct FUnrealAiToolSurfaceTelemetry;

namespace UnrealAiToolSurfacePipeline
{
	/**
	 * Tiered markdown tool index + telemetry when eligibility is not opted out (default: on; toggle ToolEligibilityTelemetryEnabled in UnrealAiRuntimeDefaults.h for legacy full index experiments).
	 * Returns false when feature off or not applicable; caller falls back to catalog defaults.
	 */
	bool TryBuildTieredToolSurface(
		const FUnrealAiAgentTurnRequest& Request,
		int32 LlmRound,
		IAgentContextService* ContextService,
		FUnrealAiToolCatalog* Catalog,
		const FUnrealAiModelCapabilities& Caps,
		const FUnrealAiToolPackOptions* PackOptions,
		bool bWantDispatchSurface,
		FString& OutToolIndexMarkdown,
		FUnrealAiToolSurfaceTelemetry& OutTelemetry,
		int32 BlueprintBuilderAppendixBudgetChars = 0,
		int32 EnvironmentBuilderAppendixBudgetChars = 0);
}
