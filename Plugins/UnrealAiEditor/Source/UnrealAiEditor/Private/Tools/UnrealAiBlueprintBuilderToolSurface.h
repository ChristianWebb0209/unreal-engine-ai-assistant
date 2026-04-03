#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"

class FUnrealAiToolCatalog;
struct FUnrealAiModelCapabilities;
struct FUnrealAiToolPackOptions;

/**
 * Blueprint Builder–specific tool-surface policy (retrieval query shaping, roster size, appendix verbosity).
 * Keeps UnrealAiToolSurfacePipeline free of builder-only branches where possible.
 */
namespace UnrealAiBlueprintBuilderToolSurface
{
	/** BM25 / lexical retrieval: extra terms so blueprint and graph tools rank sensibly. */
	void AugmentHybridRetrievalQuery(FString& InOutHybrid);

	/** Telemetry tag for tool_selector_ranks / harness metrics. */
	FString SurfaceProfileTelemetryId();

	/**
	 * After tiered ranking, move every tool_id with catalog blueprint_builder_core after guardrails (stable, sorted).
	 */
	void MergeBlueprintCoreToolsAfterGuardrails(
		const FUnrealAiToolCatalog& Catalog,
		EUnrealAiAgentMode Mode,
		const FUnrealAiModelCapabilities& Caps,
		const FUnrealAiToolPackOptions* PackOptions,
		TFunctionRef<bool(const FString& ToolId)> ToolFilter,
		const TSet<FString>& GuardrailIds,
		TArray<FString>& InOutOrdered);

	/**
	 * Builder turns: include every eligible (non–guardrail-only) tool in the roster — no Top-K truncation.
	 * Keffective becomes the count of non-guardrail tools in the scored pool.
	 */
	void WidenKeffectiveToFullEligibleRoster(int32 NonGuardrailPoolCount, int32& InOutKeffective);

	/**
	 * Compute max chars for the tiered tool appendix from model context (fraction of window, capped).
	 * Returns false with a user-facing reason if the profile window is too small for Blueprint Builder.
	 */
	bool TryComputeAppendixBudgetFromModelContext(
		int32 MaxContextTokens,
		int32 CharPerTokenApprox,
		int32& OutAppendixBudgetChars,
		FString* OutFailureReason = nullptr);

	/** Expand all roster entries; Budget/params come from TurnLlmRequestBuilder (fraction-based) or legacy ceiling. */
	void GetVerboseAppendixSettings(
		int32 OrderedToolCount,
		int32 MaxAppendixBudgetChars,
		int32& OutExpandedCount,
		int32& OutBudgetChars,
		int32& OutParametersExcerptMaxChars);

	/** Long harness user preamble for the automated sub-turn (before the inner spec). */
	FString BuildAutomatedSubturnHarnessPreamble();
}
