// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "BlueprintFormat/BlueprintGraphFormatOptions.h"

class UEdGraph;

namespace UnrealBlueprintAutoComments
{
	/** Optional region comments for large exec islands (Minimal/Verbose); never runs when CommentsMode is Off. */
	int32 MaybeAddRegionCommentsForLargeGraphs(UEdGraph* Graph, const FUnrealBlueprintGraphFormatOptions& Options);
}
