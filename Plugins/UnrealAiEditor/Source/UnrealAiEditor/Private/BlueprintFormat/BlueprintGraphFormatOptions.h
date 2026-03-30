// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

/** Horizontal strip vs stacked exec “lanes” (see BlueprintGraphStrandLayout). */
enum class EUnrealBlueprintGraphLayoutStrategy : uint8
{
	SingleRow,
	MultiStrand
};

/** Optional reroute (knot) insertion on long data links (see BlueprintGraphKnotService). */
enum class EUnrealBlueprintWireKnotAggression : uint8
{
	Off,
	Light,
	Aggressive
};

/** Controls how LayoutEntireGraph / selection / post-IR layout behave. */
struct FUnrealBlueprintGraphFormatOptions
{
	EUnrealBlueprintGraphLayoutStrategy LayoutStrategy = EUnrealBlueprintGraphLayoutStrategy::SingleRow;
	EUnrealBlueprintWireKnotAggression WireKnotAggression = EUnrealBlueprintWireKnotAggression::Off;
	/** After layout, re-snap every comment box to nodes geometrically inside it (grow/shrink). */
	bool bReflowCommentsByGeometry = true;

	static FUnrealBlueprintGraphFormatOptions Default() { return FUnrealBlueprintGraphFormatOptions(); }
};
