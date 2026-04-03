#pragma once

#include "CoreMinimal.h"

/** Parse `<unreal_ai_build_blueprint>...</unreal_ai_build_blueprint>` handoff blocks from assistant text. */
namespace UnrealAiBuildBlueprintTag
{
	/**
	 * If Content contains a well-formed build-blueprint block, returns true and sets:
	 * - OutInnerSpec: inner payload (trimmed) for the builder sub-turn user message
	 * - OutVisibleWithoutTags: assistant-visible text with the block removed (trimmed)
	 */
	bool TryConsume(const FString& Content, FString& OutInnerSpec, FString& OutVisibleWithoutTags);
}

/** Parse `<unreal_ai_blueprint_builder_result>...</unreal_ai_blueprint_builder_result>` from builder sub-turn assistant text. */
namespace UnrealAiBlueprintBuilderResultTag
{
	bool TryConsume(const FString& Content, FString& OutInnerPayload, FString& OutVisibleWithoutTags);
}
