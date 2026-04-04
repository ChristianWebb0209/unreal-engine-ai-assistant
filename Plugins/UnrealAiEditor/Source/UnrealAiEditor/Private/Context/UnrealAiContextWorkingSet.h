#pragma once

#include "Context/AgentContextTypes.h"

#include "UObject/NameTypes.h"

/** Thread MRU asset list + open-tab ordering for snapshot/autofill. */
namespace UnrealAiContextWorkingSet
{
	inline constexpr int32 MaxEntries = 16;

	void Touch(
		FAgentContextState& State,
		const FString& ObjectPath,
		const FString& AssetClassPath,
		EThreadAssetTouchSource Source,
		const FString& LastToolName = FString());

	void TouchFromToolResult(FAgentContextState& State, FName ToolName, const FString& ResultText);

	void ReorderOpenEditorAssets(FEditorContextSnapshot& Snap, const TArray<FThreadAssetWorkingEntry>& WorkingSet);

	void TouchFromEditorSnapshot(FAgentContextState& State, const FEditorContextSnapshot& Snap);

	/** Working-set paths first (MRU), then first snapshot path that looks like a writable Blueprint under /Game. */
	FString FindBlueprintPathForAutofill(const FAgentContextState& State);
}
