// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "UnrealAiEditorSettings.h" // IWYU: EUnrealAiBlueprintCommentsMode

/** Optional reroute (knot) insertion on long data links (see BlueprintGraphKnotService). */
enum class EUnrealBlueprintWireKnotAggression : uint8
{
	Off,
	Light,
	Aggressive
};

/** Controls how LayoutEntireGraph / selection / post-IR layout behave. Populated from UUnrealAiEditorSettings via UnrealAiBlueprintTools_MakeFormatOptionsFromSettings. */
struct FUnrealBlueprintGraphFormatOptions
{
	EUnrealBlueprintWireKnotAggression WireKnotAggression = EUnrealBlueprintWireKnotAggression::Off;
	/** After layout, re-snap every comment box to nodes geometrically inside it (grow/shrink). */
	bool bReflowCommentsByGeometry = true;

	/** When true, script nodes with non-zero position are excluded from repositioning. */
	bool bPreserveExistingPositions = false;

	/** Pixel spacing between layout columns (X) and rows (Y). */
	int32 SpacingX = 400;
	int32 SpacingY = 200;
	/** Extra vertical gap between branch lanes. */
	int32 BranchVerticalGap = 48;

	/** When > 0, inserts data knots on links longer than this (Manhattan pin distance). */
	int32 MaxWireLengthBeforeReroute = 0;
	/** When > 0 and geometry suggests many crossings, prefer knot insertion (approximate). */
	int32 MaxCrossingsPerSegment = 0;

	/** Experimental: orthogonal reroutes on long exec edges. */
	bool bAllowExecKnots = false;

	/** Drives auto region comments when formatter synthesizes boxes. */
	EUnrealAiBlueprintCommentsMode CommentsMode = EUnrealAiBlueprintCommentsMode::Minimal;

	static FUnrealBlueprintGraphFormatOptions Default() { return FUnrealBlueprintGraphFormatOptions(); }
};
