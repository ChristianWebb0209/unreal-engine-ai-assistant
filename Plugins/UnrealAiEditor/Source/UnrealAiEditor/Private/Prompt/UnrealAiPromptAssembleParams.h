#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"
#include "UnrealAiBlueprintBuilderTargetKind.h"
#include "UnrealAiEnvironmentBuilderTargetKind.h"

/** Parameters for assembling static prompt chunks + transcript tokens (see `prompts/README.md`). */
struct FUnrealAiPromptAssembleParams
{
	const FAgentContextBuildResult* Built = nullptr;
	EUnrealAiAgentMode Mode = EUnrealAiAgentMode::Agent;
	int32 LlmRound = 1;
	int32 MaxLlmRounds = 32;
	FString ThreadId;
	bool bIncludeExecutionSubturnChunk = false;
	bool bIncludePlanDagChunk = false;
	/** Plan-mode node execution: Agent turn on thread id `*_plan_*` (serial DAG node harness). */
	bool bIncludePlanNodeExecutionChunk = false;

	/** Blueprint Builder sub-turn: alternate prompt stack under `prompts/chunks/blueprint-builder/`. */
	bool bBlueprintBuilderMode = false;

	/** Sub-turn domain from `<unreal_ai_build_blueprint>` frontmatter; selects `blueprint-builder/kinds/*.md`. */
	EUnrealAiBlueprintBuilderTargetKind BlueprintBuilderTargetKind = EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;

	/** Main agent: one-shot resume guidance after builder result (see `13-blueprint-builder-resume.md`). */
	bool bInjectBlueprintBuilderResumeChunk = false;

	bool bEnvironmentBuilderMode = false;

	EUnrealAiEnvironmentBuilderTargetKind EnvironmentBuilderTargetKind = EUnrealAiEnvironmentBuilderTargetKind::PcgScene;

	bool bInjectEnvironmentBuilderResumeChunk = false;
};
