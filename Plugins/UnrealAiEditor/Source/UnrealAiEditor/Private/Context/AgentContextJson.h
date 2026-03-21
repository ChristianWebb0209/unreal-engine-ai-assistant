#pragma once

#include "Context/AgentContextTypes.h"

namespace UnrealAiAgentContextJson
{
	bool StateToJson(const FAgentContextState& State, FString& OutJson);
	bool JsonToState(const FString& Json, FAgentContextState& OutState, TArray<FString>& OutWarnings);
}
