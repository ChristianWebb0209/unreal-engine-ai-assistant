#pragma once

#include "CoreMinimal.h"

/**
 * Domain for Blueprint Builder / Graph Builder sub-turns (handoff `<unreal_ai_build_blueprint>`).
 * Drives conditional prompt chunks and `builder_domains` filtering in the tool catalog.
 */
enum class EUnrealAiBlueprintBuilderTargetKind : uint8
{
	/** Default: Kismet script graphs on standard Blueprints (EventGraph, etc.). */
	ScriptBlueprint = 0,
	AnimBlueprint,
	MaterialInstance,
	/** Base Material expression graphs (not K2); tools: material_graph_* . */
	MaterialGraph,
	Niagara,
	/** UMG / Widget Blueprint Designer + script — K2 for graph logic; Designer layout still limited. */
	WidgetBlueprint,
};

namespace UnrealAiBlueprintBuilderTargetKind
{
	/** Parse from YAML value or JSON string (snake_case). */
	EUnrealAiBlueprintBuilderTargetKind ParseFromString(const FString& In);

	/** Canonical catalog / chunk suffix: script_blueprint, anim_blueprint, ... */
	FString ToDomainString(EUnrealAiBlueprintBuilderTargetKind Kind);

	/** Chunk file under prompts/chunks/blueprint-builder/kinds/<name>.md */
	FString KindChunkFileName(EUnrealAiBlueprintBuilderTargetKind Kind);
}
