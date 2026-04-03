#pragma once

#include "CoreMinimal.h"

struct FUnrealAiAgentTurnRequest;

/**
 * Controls which Blueprint graph-mutation tools appear in the LLM tool index.
 * Main Agent turns omit tools that are reserved for automated Blueprint Builder sub-turns.
 */
namespace UnrealAiBlueprintToolGate
{
	bool PassesToolSurfaceFilter(const FUnrealAiAgentTurnRequest& Request, const FString& ToolId);
}
