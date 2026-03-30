#pragma once

#include "CoreMinimal.h"
#include "BlueprintFormat/BlueprintGraphFormatService.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/** Facade for in-process Blueprint graph layout (vendored formatter). */
namespace UnrealAiBlueprintFormatterBridge
{
	bool IsLayoutServiceAvailable();

	/** @deprecated Same as IsLayoutServiceAvailable; kept for call-site stability. */
	bool IsFormatterPluginEnabled();

	/** @deprecated Same as IsLayoutServiceAvailable; kept for call-site stability. */
	bool IsFormatterModuleReady();

	/** @deprecated Always succeeds; OutHint cleared. */
	bool EnsureFormatterModuleLoaded(FString* OutHint = nullptr);

	/** @deprecated Empty; formatter is bundled in this module. */
	FString FormatterInstallHint();

	FUnrealBlueprintGraphFormatResult TryLayoutAfterAiIrApply(
		UEdGraph* Graph,
		const TArray<UEdGraphNode*>& MaterializedNodes,
		const TArray<FUnrealBlueprintIrNodeLayoutHint>& Hints,
		bool bWanted);

	FUnrealBlueprintGraphFormatResult TryLayoutEntireGraph(UEdGraph* Graph, bool bWanted);

	int32 TryLayoutAllScriptGraphs(UBlueprint* BP);
}
