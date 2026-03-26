#pragma once

#include "Context/AgentContextTypes.h"

namespace UnrealAiAgentContextFormat
{
	/** e.g. "Unreal Engine 5.7" from the running editor (major.minor). */
	FString GetProjectEngineVersionLabel();

	/** Apply v1 policy: drop tool results for Ask mode in options; trim tool entries per policy. */
	void ApplyModeToStateForBuild(FAgentContextState& State, const FAgentContextBuildOptions& Options);

	/** Rough token estimate for logging / future budgets. */
	int32 EstimateTokensApprox(const FString& Text);
}
