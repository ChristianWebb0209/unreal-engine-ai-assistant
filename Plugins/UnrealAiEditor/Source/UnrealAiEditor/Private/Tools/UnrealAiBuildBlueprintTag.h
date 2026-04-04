#pragma once

#include "CoreMinimal.h"
#include "UnrealAiBlueprintBuilderTargetKind.h"

/** Parse `<unreal_ai_build_blueprint>...</unreal_ai_build_blueprint>` handoff blocks from assistant text. */
namespace UnrealAiBuildBlueprintTag
{
	/**
	 * If Content contains a well-formed build-blueprint block, returns true and sets:
	 * - OutInnerSpec: inner payload (trimmed) for the builder sub-turn user message
	 * - OutVisibleWithoutTags: assistant-visible text with the block removed (trimmed)
	 */
	bool TryConsume(const FString& Content, FString& OutInnerSpec, FString& OutVisibleWithoutTags);

	/**
	 * Reads optional YAML frontmatter (`---` ... `---`) with `target_kind: <domain>` or a leading `target_kind:` line.
	 * Strips that metadata from InOutInner. Defaults to ScriptBlueprint when absent or unparsable.
	 */
	void ParseAndStripHandoffMetadata(FString& InOutInner, EUnrealAiBlueprintBuilderTargetKind& OutKind);

	/**
	 * Removes harness protocol tag literals from user-visible text (case-insensitive).
	 * Use when the model emits orphan/closing fragments or mixed tags that TryConsume does not remove.
	 */
	void StripProtocolMarkersForUi(FString& InOutText);
}

/** Parse `<unreal_ai_blueprint_builder_result>...</unreal_ai_blueprint_builder_result>` from builder sub-turn assistant text. */
namespace UnrealAiBlueprintBuilderResultTag
{
	bool TryConsume(const FString& Content, FString& OutInnerPayload, FString& OutVisibleWithoutTags);
}
