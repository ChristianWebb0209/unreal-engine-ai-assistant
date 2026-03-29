#pragma once

#include "CoreMinimal.h"

/** Shared visual tokens for plan-mode DAG UI (matches Plan accent in SChatComposer mode picker). */
struct FUnrealAiPlanUiTokens
{
	static FLinearColor PlanAccent()
	{
		return FLinearColor(0.86f, 0.56f, 0.20f, 1.f);
	}
};
