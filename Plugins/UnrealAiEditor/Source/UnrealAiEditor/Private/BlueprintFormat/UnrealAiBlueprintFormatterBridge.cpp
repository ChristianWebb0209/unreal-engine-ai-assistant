#include "BlueprintFormat/UnrealAiBlueprintFormatterBridge.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

bool UnrealAiBlueprintFormatterBridge::IsLayoutServiceAvailable()
{
	return true;
}

bool UnrealAiBlueprintFormatterBridge::IsFormatterPluginEnabled()
{
	return IsLayoutServiceAvailable();
}

bool UnrealAiBlueprintFormatterBridge::IsFormatterModuleReady()
{
	return IsLayoutServiceAvailable();
}

bool UnrealAiBlueprintFormatterBridge::EnsureFormatterModuleLoaded(FString* OutHint)
{
	if (OutHint)
	{
		OutHint->Reset();
	}
	return IsLayoutServiceAvailable();
}

FString UnrealAiBlueprintFormatterBridge::FormatterInstallHint()
{
	return FString();
}

FUnrealBlueprintGraphFormatResult UnrealAiBlueprintFormatterBridge::TryLayoutAfterAiIrApply(
	UEdGraph* Graph,
	const TArray<UEdGraphNode*>& MaterializedNodes,
	const TArray<FUnrealBlueprintIrNodeLayoutHint>& Hints,
	bool bWanted,
	const FUnrealBlueprintGraphFormatOptions& Options)
{
	FUnrealBlueprintGraphFormatResult R;
	if (!bWanted || !Graph)
	{
		return R;
	}
	return FUnrealBlueprintGraphFormatService::LayoutAfterAiIrApply(Graph, MaterializedNodes, Hints, Options);
}

FUnrealBlueprintGraphFormatResult UnrealAiBlueprintFormatterBridge::TryLayoutEntireGraph(
	UEdGraph* Graph,
	bool bWanted,
	const FUnrealBlueprintGraphFormatOptions& Options)
{
	FUnrealBlueprintGraphFormatResult R;
	if (!bWanted || !Graph)
	{
		return R;
	}
	return FUnrealBlueprintGraphFormatService::LayoutEntireGraph(Graph, Options);
}

FUnrealBlueprintGraphFormatResult UnrealAiBlueprintFormatterBridge::TryLayoutSelectedNodes(
	UEdGraph* Graph,
	const TArray<UEdGraphNode*>& SelectedNodes,
	bool bWanted,
	const FUnrealBlueprintGraphFormatOptions& Options)
{
	FUnrealBlueprintGraphFormatResult R;
	if (!bWanted || !Graph)
	{
		return R;
	}
	return FUnrealBlueprintGraphFormatService::LayoutSelectedNodes(Graph, SelectedNodes, Options);
}

int32 UnrealAiBlueprintFormatterBridge::TryLayoutAllScriptGraphs(UBlueprint* BP, const FUnrealBlueprintGraphFormatOptions& Options)
{
	if (!BP)
	{
		return 0;
	}
	int32 Count = 0;
	auto LayoutIfHasNodes = [&Count, &Options](UEdGraph* G)
	{
		if (!G)
		{
			return;
		}
		bool bAny = false;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (N)
			{
				bAny = true;
				break;
			}
		}
		if (!bAny)
		{
			return;
		}
		FUnrealBlueprintGraphFormatService::LayoutEntireGraph(G, Options);
		++Count;
	};
	for (const TObjectPtr<UEdGraph>& G : BP->UbergraphPages)
	{
		LayoutIfHasNodes(G.Get());
	}
	for (const TObjectPtr<UEdGraph>& G : BP->FunctionGraphs)
	{
		LayoutIfHasNodes(G.Get());
	}
	for (const TObjectPtr<UEdGraph>& G : BP->MacroGraphs)
	{
		LayoutIfHasNodes(G.Get());
	}
	if (Count > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}
	return Count;
}
