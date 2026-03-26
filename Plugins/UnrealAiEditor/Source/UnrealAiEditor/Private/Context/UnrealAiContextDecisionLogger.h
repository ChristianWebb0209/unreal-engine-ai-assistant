#pragma once

#include "Context/AgentContextTypes.h"
#include "Context/UnrealAiContextCandidates.h"

namespace UnrealAiContextDecisionLogger
{
	bool ShouldLogDecisions(bool bVerboseContextBuild);

	void WriteDecisionLog(
		const FString& ProjectId,
		const FString& ThreadId,
		const FString& InvocationReason,
		const FAgentContextBuildOptions& Options,
		int32 BudgetChars,
		const UnrealAiContextCandidates::FUnifiedContextBuildResult& Unified,
		const FString& EmittedContextBlock);
}
