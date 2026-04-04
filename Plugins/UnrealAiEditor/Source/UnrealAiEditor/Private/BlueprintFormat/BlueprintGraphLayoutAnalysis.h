// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UEdGraph;

namespace UnrealBlueprintGraphLayoutAnalysis
{
	/** Read-only metrics for LLM / formatter diagnostics (optional include_layout_analysis on blueprint_get_graph_summary). */
	void AppendToJsonObject(UEdGraph* Graph, TSharedPtr<FJsonObject> Target);
}
