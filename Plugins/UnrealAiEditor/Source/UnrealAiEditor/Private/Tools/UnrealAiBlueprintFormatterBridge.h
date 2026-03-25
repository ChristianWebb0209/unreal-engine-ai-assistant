#pragma once

#include "CoreMinimal.h"
#include "BlueprintGraphFormatService.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/**
 * Ensures the sibling UnrealBlueprintFormatter plugin is present and exposes safe wrappers
 * so blueprint tools still succeed when the plugin is missing (layout skipped + hints for the model).
 */
namespace UnrealAiBlueprintFormatterBridge
{
	/** Plugin descriptor exists and is enabled in this project. */
	bool IsFormatterPluginEnabled();

	/** Plugin enabled and formatter module is loaded. */
	bool IsFormatterModuleReady();

	/**
	 * Load the formatter editor module if the plugin is enabled.
	 * @param OutHint Optional human-readable reason when false (for tool JSON / logs).
	 */
	bool EnsureFormatterModuleLoaded(FString* OutHint = nullptr);

	FString FormatterInstallHint();

	/** Editor toast once per session when formatter is unavailable (no-op if Slate not ready). */
	void NotifyFormatterMissingOnce();

	/** Log + optional toast — call from UnrealAiEditor module startup. */
	void ValidatePluginDependencyOnStartup();

	/**
	 * Runs LayoutAfterAiIrApply when bWanted and formatter loads; otherwise returns empty result
	 * or adds Warnings entries with install instructions.
	 */
	FUnrealBlueprintGraphFormatResult TryLayoutAfterAiIrApply(
		UEdGraph* Graph,
		const TArray<UEdGraphNode*>& MaterializedNodes,
		const TArray<FUnrealBlueprintIrNodeLayoutHint>& Hints,
		bool bWanted);

	/** Layout every node in a single graph (full-graph pass). */
	FUnrealBlueprintGraphFormatResult TryLayoutEntireGraph(UEdGraph* Graph, bool bWanted);

	/**
	 * Layout ubergraph + function + macro graphs (non-empty). Returns count of graphs passed to LayoutEntireGraph.
	 * 0 if formatter unavailable or BP null.
	 */
	int32 TryLayoutAllScriptGraphs(UBlueprint* BP);
}
