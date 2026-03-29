#pragma once

#include "Context/AgentContextTypes.h"
#include "CoreMinimal.h"

/** Small helpers shared by FUnrealAiAgentHarness for Plan-mode (chat planner DAG) behavior. */
namespace UnrealAiPlanPlannerHarness
{
	inline bool IsPlanPlannerPass(EUnrealAiAgentMode Mode)
	{
		return Mode == EUnrealAiAgentMode::Plan;
	}

	/** User nudge when the planner returns an empty assistant message mid-round. */
	FString GetEmptyPlannerNudgeUserMessage();
}
