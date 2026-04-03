#pragma once

#include "CoreMinimal.h"

/**
 * Central hardcoded defaults for UnrealAiEditor runtime behavior (non-wait settings).
 * Wall-clock / harness wait budgets live in UnrealAiWaitTimePolicy.h (namespace UnrealAiWaitTime).
 */
namespace UnrealAiRuntimeDefaults
{
	// --- FAgentRunFileSink / context dumps ---
	inline constexpr bool ContextVerboseDefault = false;
	/** Verbose context_build_trace lines (large; opt-in via UNREAL_AI_HARNESS_VERBOSE_CONTEXT=1). */
	inline constexpr bool HarnessContextVerboseDefault = false;
	/** Dump context at run_finished (blocks DoneEvent; opt-in via UNREAL_AI_HARNESS_DUMP_CONTEXT_ON_FINISH=1). */
	inline constexpr bool HarnessRunFinishedContextDump = false;
	/** When RunAgentTurn has no dumpcontext|nodump 5th arg, mirror long-running harness default. */
	inline constexpr bool HarnessDumpContextAfterEachToolDefault = true;

	// --- FUnrealAiAgentHarness: tool telemetry / repair nudges ---
	inline constexpr bool ToolUsageLogEnabled = true;
	inline constexpr bool ToolRepairNudgeEnabled = true;
	/** Emit tool_selector_ranks JSON in harness run.jsonl (full K roster + scores). */
	inline constexpr bool ToolSelectorVerboseLogEnabled = true;

	// --- Memory compaction triggers (FUnrealAiMemoryCompactor path) ---
	inline constexpr int32 MemoryCompactTurnInterval = 4;
	inline constexpr int32 MemoryCompactTokenThreshold = 3000;
	inline constexpr int32 MemoryCompactPromptThreshold = 1800;
	inline constexpr int32 MemoryCompactHistoryMessages = 12;
	inline constexpr int32 MemoryCompactMaxBodyChars = 2400;
	inline constexpr int32 MemoryCompactMaxCreate = 1;
	inline constexpr int32 MemoryCompactMinConfidencePercent = 55;
	inline constexpr int32 MemoryPruneMaxItems = 120;
	inline constexpr int32 MemoryPruneRetentionDays = 90;
	inline constexpr int32 MemoryPruneMinConfidencePercent = 30;

	inline constexpr int32 ToolRepairMaxPerUserSend = 1;

	// --- UnrealAiTurnLlmRequestBuilder (headed harness historically used core + extras for smaller payloads) ---
	inline constexpr bool ToolPackRestrictToCore = true;
	inline const TCHAR* ToolPackExtraCommaSeparated =
		TEXT("asset_create,asset_registry_query,blueprint_compile,blueprint_set_component_default,blueprint_apply_ir,blueprint_graph_patch,blueprint_graph_list_pins,"
			 "blueprint_export_ir,blueprint_get_graph_summary,blueprint_open_graph_tab,"
			 "blueprint_add_variable,audio_component_preview,project_file_read_text,project_file_write_text,project_file_move,"
			 "cpp_project_compile,actor_spawn_from_class,console_command");
	inline constexpr bool ToolSurfaceUseDispatch = true; // false = native full schema per tool in tools[]
	inline constexpr bool HarnessOmitTools = false;

	// --- UnrealAiToolSurfacePipeline ---
	inline constexpr bool ToolEligibilityTelemetryEnabled = true;
	inline constexpr bool ToolUsagePriorEnabled = true;
	inline constexpr int32 ToolKMin = 6;
	inline constexpr int32 ToolKMax = 24;
	inline constexpr int32 ToolExpandedCount = 4;
	inline constexpr int32 ToolSurfaceBudgetChars = 80000;
	/** Blueprint Builder automated sub-turn: tiered tool appendix (verbose roster; all tools expanded when possible). */
	inline constexpr int32 BlueprintBuilderToolSurfaceBudgetChars = 240000;
	/** Max fraction of estimated model context window (chars) allocated to the tiered tool appendix for Blueprint Builder. */
	inline constexpr float BlueprintBuilderToolSurfaceMaxContextFraction = 0.35f;
	/** If computed appendix budget is below this, TurnLlmRequestBuilder fails with a clear error (context too small). */
	inline constexpr int32 BlueprintBuilderToolSurfaceMinBudgetChars = 16000;
	/**
	 * When model profile MaxContextTokens is unset (<=0), assume this many tokens only for appendix budget math
	 * (does not change HTTP request; avoids degenerate tiny budgets).
	 */
	inline constexpr int32 BlueprintBuilderFallbackContextTokensWhenUnset = 128000;
	/** Max chars of per-tool parameters JSON embedded in the builder appendix (0 = no truncation). */
	inline constexpr int32 BlueprintBuilderToolParamsExcerptMaxChars = 0;

	// --- UnrealAiToolDispatch destructive tools (headed runs: auto-confirm destructive tools) ---
	inline constexpr bool AutoRunDestructiveDefault = true;

	// --- UnrealAiContextDecisionLogger ---
	inline constexpr bool ContextDecisionLogEnabled = true;

	// --- UnrealAiHarnessTpmThrottle ---
	inline constexpr int32 HarnessTpmWindowSec = 60;
	inline constexpr bool HarnessTpmStrict = true;
	inline constexpr int32 HarnessTpmHeadroomTokens = 0;
	inline constexpr int32 HarnessTpmPerMinute = 200000;
	inline constexpr int32 HarnessEmbeddingTpmPerMinute = 200000;
	inline constexpr int32 HarnessTpmPromptDivisor = 3;
	inline constexpr int32 HarnessTpmChatOverheadTokens = 512;
	inline constexpr int32 HarnessTpmEmbedOverheadTokens = 64;
	inline constexpr int32 HarnessTpmEstimateSafetyBpStrict = 125;
	inline constexpr int32 HarnessTpmEstimateSafetyBpLoose = 110;

}
