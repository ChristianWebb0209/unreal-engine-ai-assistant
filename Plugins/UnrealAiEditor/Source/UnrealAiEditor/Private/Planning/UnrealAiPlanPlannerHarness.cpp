#include "Planning/UnrealAiPlanPlannerHarness.h"

FString UnrealAiPlanPlannerHarness::GetEmptyPlannerNudgeUserMessage()
{
	return TEXT(
		"[Harness][reason=empty_planner] The model returned an empty assistant message. Output a single JSON object for the plan "
		"(schema unreal_ai.plan_dag, nodes array with id/title/hint/dependsOn). No tools—JSON only.");
}
