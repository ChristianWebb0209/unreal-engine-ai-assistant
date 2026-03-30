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
	bool bWanted)
{
	FUnrealBlueprintGraphFormatResult R;
	if (!bWanted || !Graph)
	{
		return R;
	}
	return FUnrealBlueprintGraphFormatService::LayoutAfterAiIrApply(Graph, MaterializedNodes, Hints);
}

FUnrealBlueprintGraphFormatResult UnrealAiBlueprintFormatterBridge::TryLayoutEntireGraph(UEdGraph* Graph, bool bWanted)
{
	FUnrealBlueprintGraphFormatResult R;
	if (!bWanted || !Graph)
	{
		return R;
	}
	FUnrealBlueprintGraphFormatService::LayoutEntireGraph(Graph);
	R.NodesPositioned = 0;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (N)
		{
			++R.NodesPositioned;
		}
	}
	return R;
}

int32 UnrealAiBlueprintFormatterBridge::TryLayoutAllScriptGraphs(UBlueprint* BP)
{
	if (!BP)
	{
		return 0;
	}
	int32 Count = 0;
	auto LayoutIfHasNodes = [&Count](UEdGraph* G)
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
		FUnrealBlueprintGraphFormatService::LayoutEntireGraph(G);
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
