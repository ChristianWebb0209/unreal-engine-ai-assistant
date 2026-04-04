// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "BlueprintFormat/BlueprintGraphFormatOptions.h"

class UEdGraph;
class UEdGraphNode;

namespace UnrealBlueprintLayeredDagLayout
{
	struct FLayeredLayoutStats
	{
		int32 EntryPoints = 0;
		int32 DisconnectedNodes = 0;
		int32 DataOnlyNodes = 0;
		int32 LayoutNodes = 0;
		int32 SkippedPreserve = 0;
	};

	/** Layer assignment with cycle relaxation + light crossing reduction + branch vertical gap. */
	void LayoutScriptNodes(
		UEdGraph* Graph,
		const TArray<UEdGraphNode*>& ScriptNodes,
		const FUnrealBlueprintGraphFormatOptions& Options,
		FLayeredLayoutStats& OutStats);
}
