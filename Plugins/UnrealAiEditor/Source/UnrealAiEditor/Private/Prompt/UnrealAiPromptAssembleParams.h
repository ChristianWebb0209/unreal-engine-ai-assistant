#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"
#include "UnrealAiBlueprintBuilderTargetKind.h"
#include "UnrealAiProductSpecialistId.h"

/** Parameters for assembling static prompt chunks + transcript tokens (see `prompts/README.md`). */
struct FUnrealAiPromptAssembleParams
{
	const FAgentContextBuildResult* Built = nullptr;
	EUnrealAiAgentMode Mode = EUnrealAiAgentMode::Agent;
	int32 LlmRound = 1;
	int32 MaxLlmRounds = 2048;
	FString ThreadId;
	bool bIncludeExecutionSubturnChunk = false;
	bool bIncludePlanDagChunk = false;
	/** Plan-mode node execution: Agent turn on thread id `*_plan_*` (serial DAG node harness). */
	bool bIncludePlanNodeExecutionChunk = false;

	/** Blueprint Builder sub-turn: `prompts/chunks/common/*` prefix + `prompts/chunks/blueprint-builder/*` domain stack. */
	bool bBlueprintBuilderMode = false;

	/** Sub-turn domain from `<unreal_ai_build_blueprint>` frontmatter; selects `blueprint-builder/kinds/*.md`. */
	EUnrealAiBlueprintBuilderTargetKind BlueprintBuilderTargetKind = EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;

	/** Main agent: one-shot resume guidance after builder result (`blueprint-builder/09-resume-on-main-agent.md`). */
	bool bInjectBlueprintBuilderResumeChunk = false;

	/** Scene / future specialists: dedicated prompt stack + pinned tools. */
	EUnrealAiProductSpecialistId ActiveProductSpecialistId = EUnrealAiProductSpecialistId::None;

	/**
	 * Agent orchestrator lane (not builder, not specialist, not plan-node): thin routing stack under `prompts/chunks/orchestrator/` plus builder delegation excerpts.
	 */
	bool bOrchestratorAgentTurn = false;

	bool bInjectProductSpecialistResumeChunk = false;

	/** Verbatim orchestrator brief for the active specialist turn (`{{SPECIALIST_DELEGATION_BRIEF}}`). */
	FString SpecialistDelegationBrief;
};
