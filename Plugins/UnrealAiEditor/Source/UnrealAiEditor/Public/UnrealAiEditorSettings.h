#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UnrealAiEditorSettings.generated.h"

/** How the agent should treat Blueprint graph commentary (section labels, future comment-box IR). */
UENUM()
enum class EUnrealAiBlueprintCommentsMode : uint8
{
	Off UMETA(DisplayName = "Off"),
	Minimal UMETA(DisplayName = "Minimal"),
	Verbose UMETA(DisplayName = "Verbose")
};

UCLASS(Config = Editor, defaultconfig, meta = (DisplayName = "Unreal AI Editor"))
class UUnrealAiEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UUnrealAiEditorSettings(const FObjectInitializer& ObjectInitializer);

	virtual FName GetCategoryName() const override;

	UPROPERTY(EditAnywhere, Config, Category = "Agent")
	FString DefaultAgentId;

	UPROPERTY(EditAnywhere, Config, Category = "Agent")
	bool bAutoConnect;

	UPROPERTY(EditAnywhere, Config, Category = "Agent")
	bool bVerboseLogging;

	/** Open Agent Chat automatically when the Level Editor finishes loading (Nomad tab). */
	UPROPERTY(EditAnywhere, Config, Category = "UI", meta = (DisplayName = "Open Agent Chat on editor startup"))
	bool bOpenAgentChatOnStartup;

	/** Stream LLM responses for smoother chat output (recommended). */
	UPROPERTY(EditAnywhere, Config, Category = "Chat")
	bool bStreamLlmChat;

	/** Reveal assistant text with a typewriter effect (also smooths single-chunk responses). */
	UPROPERTY(EditAnywhere, Config, Category = "Chat")
	bool bAssistantTypewriter;

	/** Characters per second for typewriter mode. */
	UPROPERTY(EditAnywhere, Config, Category = "Chat", meta = (ClampMin = "10", ClampMax = "8000"))
	float AssistantTypewriterCps;

	/** When off, the harness should pause between plan steps (UI hint; harness wiring may follow). */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (DisplayName = "Auto-continue plan steps"))
	bool bAutoContinuePlanSteps;

	/**
	 * When true, plan mode may run an extra Plan-mode LLM pass after a node failure or headed scenario wall stall
	 * (between sub-turns) to merge a revised DAG for remaining work. See docs/todo.md (automatic replan).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (DisplayName = "Plan: auto-replan on failure or wall stall"))
	bool bPlanAutoReplan = true;

	/** Off: no auto region comments; Minimal / Verbose: formatter may add section boxes (see Blueprint formatter). */
	UPROPERTY(EditAnywhere, Config, Category = "Blueprint Formatting", meta = (DisplayName = "Comment synthesis"))
	EUnrealAiBlueprintCommentsMode BlueprintCommentsMode = EUnrealAiBlueprintCommentsMode::Minimal;

	/** Insert reroute knots on long or crowded data wires (best-effort). */
	UPROPERTY(EditAnywhere, Config, Category = "Blueprint Formatting", meta = (DisplayName = "Use wire knots"))
	bool bBlueprintFormatUseWireKnots = false;

	/** Skip repositioning nodes that already have non-zero graph coordinates. */
	UPROPERTY(EditAnywhere, Config, Category = "Blueprint Formatting", meta = (DisplayName = "Preserve existing node positions"))
	bool bBlueprintFormatPreserveExistingPositions = false;

	/** After layout, resize comment boxes to fit member nodes geometrically. */
	UPROPERTY(EditAnywhere, Config, Category = "Blueprint Formatting", meta = (DisplayName = "Reflow comment boxes to fit nodes"))
	bool bBlueprintFormatReflowCommentsByGeometry = true;

	/** Maximum supplemental planner (replan) HTTP turns per plan executor run; 0 disables replanning. */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (ClampMin = "0", ClampMax = "8"))
	int32 PlanAutoReplanMaxAttemptsPerRun = 2;

	/**
	 * Folder name segments used when filtering /Game/... package paths for **preferred create path** inference
	 * (project tree sampler + context blurb + asset_create default package_path for Blueprints).
	 * If a path contains `/Segment/`, ends with `/Segment`, or equals `/Game/Segment`, it is excluded from counts
	 * so sandbox/test content (e.g. qualitative harness output) does not outvote real game folders.
	 * Default includes UnrealAiHarness; add more (e.g. temp sandbox names) or clear to disable filtering.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (DisplayName = "Exclude folders from preferred content-path inference"))
	TArray<FString> AgentContentPathExcludeFromPreferredInference;

	/**
	 * Headed harness only: append each outbound LLM payload (messages + tools + model) to llm_requests.jsonl beside run.jsonl.
	 * Overridden when environment variable UNREAL_AI_LOG_LLM_REQUESTS is set to 1/true or 0/false.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Harness", meta = (DisplayName = "Log LLM requests to harness run folder"))
	bool bLogLlmRequestsToHarnessRunFile = true;

	/**
	 * When true, console_command passes the command string through to GEngine->Exec with minimal blocking (unsafe).
	 * Default false: only allow-listed keys from UnrealAiToolDispatch_Console.cpp are executed.
	 * Overridden when UNREAL_AI_CONSOLE_COMMAND_LEGACY_EXEC is 1/true or 0/false.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (DisplayName = "Console command: legacy wide exec (unsafe)"))
	bool bConsoleCommandLegacyWideExec = false;

	/** When on (default), record scoped timings and log each scope — see Output Log category LogUnrealAiPerf. Override with `unrealai.GtPerf 0` (off) or `1` (on). */
	UPROPERTY(EditAnywhere, Config, Category = "Diagnostics", meta = (DisplayName = "Game-thread perf scopes"))
	bool bGameThreadPerfLogging = true;

	/**
	 * Reserved for future filtering; scopes are always logged when perf is enabled. Hitch tagging still uses
	 * GameThreadPerfHitchThresholdMs.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Diagnostics", meta = (ClampMin = "0", ClampMax = "1000"))
	float GameThreadPerfLogThresholdMs = 0.f;

	/** At or above this duration (ms), log lines are tagged as a hitch (same channel, easy grep). */
	UPROPERTY(EditAnywhere, Config, Category = "Diagnostics", meta = (ClampMin = "1", ClampMax = "120000"))
	float GameThreadPerfHitchThresholdMs = 1000.f;

	/** Max rows kept for `UnrealAi.GtPerf.Dump` (oldest dropped). */
	UPROPERTY(EditAnywhere, Config, Category = "Diagnostics", meta = (ClampMin = "32", ClampMax = "4096"))
	int32 GameThreadPerfRingMax = 256;

	/** Reject blueprint_graph_patch when ops[].Num() exceeds this (split batches or use ops_json_path). */
	UPROPERTY(EditAnywhere, Config, Category = "Blueprint Tools", meta = (ClampMin = "8", ClampMax = "2048"))
	int32 MaxOpsPerBlueprintGraphPatch = 128;

	/**
	 * When true, blueprint_graph_patch (non-validate_only) runs the same simulation as validate_only before opening
	 * the edit transaction. Disable only for automation that already validated the same payload.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Blueprint Tools", meta = (DisplayName = "Graph patch: internal validate before apply"))
	bool bBlueprintGraphPatchInternalValidateBeforeApply = true;

	/**
	 * When true, if op i fails during a real apply, ops 0..i-1 stay committed (transaction is not cancelled).
	 * When false, the entire batch rolls back on the first failing op (strict atomicity).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Blueprint Tools", meta = (DisplayName = "Graph patch: keep successful ops on failure"))
	bool bBlueprintGraphPatchKeepOpsOnFailure = true;
};
