// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "BlueprintFormat/BlueprintGraphFormatOptions.h"

class UEdGraph;

namespace UnrealBlueprintKnotService
{
	/** Insert UK2Node_Knot on long straight data links (exec links skipped). Returns knots created. */
	int32 InsertDataWireKnots(UEdGraph* Graph, EUnrealBlueprintWireKnotAggression Aggression);
}
