#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UnrealAiEditorSettings.generated.h"

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

	/** Maximum supplemental planner (replan) HTTP turns per plan executor run; 0 disables replanning. */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (ClampMin = "0", ClampMax = "8"))
	int32 PlanAutoReplanMaxAttemptsPerRun = 2;

	/**
	 * Headed harness only: append each outbound LLM payload (messages + tools + model) to llm_requests.jsonl beside run.jsonl.
	 * Overridden when environment variable UNREAL_AI_LOG_LLM_REQUESTS is set to 1/true or 0/false.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Harness", meta = (DisplayName = "Log LLM requests to harness run folder"))
	bool bLogLlmRequestsToHarnessRunFile = true;
};
