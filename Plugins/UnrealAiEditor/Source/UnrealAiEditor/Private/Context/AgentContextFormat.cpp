#include "Context/AgentContextFormat.h"

#include "Misc/EngineVersion.h"

namespace UnrealAiAgentContextFormat
{
	FString GetProjectEngineVersionLabel()
	{
		const FEngineVersion& E = FEngineVersion::Current();
		return FString::Printf(TEXT("Unreal Engine %d.%d"), E.GetMajor(), E.GetMinor());
	}

	void ApplyModeToStateForBuild(FAgentContextState& State, const FAgentContextBuildOptions& Options)
	{
		if (Options.Mode == EUnrealAiAgentMode::Ask)
		{
			State.ToolResults.Reset();
		}
	}

	int32 EstimateTokensApprox(const FString& Text)
	{
		return FMath::Max(1, Text.Len() / 4);
	}
}
