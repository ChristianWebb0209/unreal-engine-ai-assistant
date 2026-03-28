#pragma once

#include "CoreMinimal.h"

/** §2.12 — margin-based dynamic K with min/max caps (deterministic for tests). */
namespace UnrealAiToolKPolicy
{
	int32 ComputeEffectiveK(const TArray<float>& SortedDescScoresNonGuard, int32 KMin, int32 KMax);
}
