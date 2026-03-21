#pragma once

#include "Context/AgentContextTypes.h"

namespace UnrealAiAgentContextFormat
{
	/** Single formatted block from state (before global budget trim). */
	FString FormatContextBlock(const FAgentContextState& State, const FAgentContextBuildOptions& Options);

	/** Trim entire string to MaxChars (UTF-16 code units); sets OutWarnings. */
	FString TruncateToBudget(const FString& Text, int32 MaxChars, TArray<FString>& OutWarnings);

	/** Apply v1 policy: drop tool results for Ask mode in options; trim tool entries per policy. */
	void ApplyModeToStateForBuild(FAgentContextState& State, const FAgentContextBuildOptions& Options);

	/** Rough token estimate for logging / future budgets. */
	int32 EstimateTokensApprox(const FString& Text);
}
