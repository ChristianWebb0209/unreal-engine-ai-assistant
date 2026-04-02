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

/** Default graph layout when tools omit layout_mode (bundled formatter). */
UENUM()
enum class EUnrealAiBlueprintDefaultLayoutStrategy : uint8
{
	SingleRow UMETA(DisplayName = "Single row"),
	MultiStrand UMETA(DisplayName = "Multi-strand (stacked lanes)")
};

/** Default data-wire reroute (knot) insertion when tools omit wire_knots. */
UENUM()
enum class EUnrealAiBlueprintDefaultWireKnotMode : uint8
{
	Off UMETA(DisplayName = "Off"),
	Light UMETA(DisplayName = "Light"),
	Aggressive UMETA(DisplayName = "Aggressive")
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

	UPROPERTY(EditAnywhere, Config, Category = "OpenRouter")
	FString OpenRouterApiKey;

	UPROPERTY(EditAnywhere, Config, Category = "OpenRouter")
	FString DefaultModelId;

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

	/**
	 * Blueprint graph commentary policy for agent turns (injected into the static prompt).
	 * Off: logic-only; Minimal: short labels where helpful; Verbose: richer sectioning guidance.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (DisplayName = "Blueprint comments"))
	EUnrealAiBlueprintCommentsMode BlueprintCommentsMode = EUnrealAiBlueprintCommentsMode::Minimal;

	/** Used when blueprint_apply_ir / blueprint_format_graph omit layout_mode. */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (DisplayName = "Blueprint layout: default strategy"))
	EUnrealAiBlueprintDefaultLayoutStrategy BlueprintDefaultLayoutStrategy = EUnrealAiBlueprintDefaultLayoutStrategy::SingleRow;

	/** Used when blueprint_apply_ir / blueprint_format_graph omit wire_knots. */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (DisplayName = "Blueprint layout: data wire knots"))
	EUnrealAiBlueprintDefaultWireKnotMode BlueprintDefaultWireKnotMode = EUnrealAiBlueprintDefaultWireKnotMode::Off;

	/** Maximum supplemental planner (replan) HTTP turns per plan executor run; 0 disables replanning. */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (ClampMin = "0", ClampMax = "8"))
	int32 PlanAutoReplanMaxAttemptsPerRun = 2;

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
};
