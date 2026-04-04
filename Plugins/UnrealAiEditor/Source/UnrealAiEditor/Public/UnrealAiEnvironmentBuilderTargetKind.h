#pragma once

#include "CoreMinimal.h"

/**
 * Domain for Environment / PCG Builder sub-turns (handoff `<unreal_ai_build_environment>`).
 * Selects `prompts/chunks/environment-builder/kinds/*.md` and `environment_builder_core` tool merge hints.
 */
enum class EUnrealAiEnvironmentBuilderTargetKind : uint8
{
	/** PCG graph execution, PCG actors, volumes. */
	PcgScene = 0,
	LandscapeTerrain,
	FoliageScatter,
	/** Mixed terrain + PCG + foliage in one pass. */
	Mixed,
};

namespace UnrealAiEnvironmentBuilderTargetKind
{
	EUnrealAiEnvironmentBuilderTargetKind ParseFromString(const FString& In);

	FString ToDomainString(EUnrealAiEnvironmentBuilderTargetKind Kind);

	FString KindChunkFileName(EUnrealAiEnvironmentBuilderTargetKind Kind);
}
