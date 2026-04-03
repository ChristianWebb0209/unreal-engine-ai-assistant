#include "Tools/UnrealAiBlueprintToolGate.h"

#include "Context/AgentContextTypes.h"
#include "Harness/UnrealAiAgentTypes.h"

bool UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(const FUnrealAiAgentTurnRequest& Request, const FString& ToolId)
{
	if (Request.Mode != EUnrealAiAgentMode::Agent)
	{
		return true;
	}
	if (Request.bBlueprintBuilderTurn || !Request.bOmitMainAgentBlueprintMutationTools)
	{
		return true;
	}

	static const TSet<FString> MainAgentHiddenBlueprintTools = {
		TEXT("blueprint_graph_patch"),
		TEXT("blueprint_format_graph"),
		TEXT("blueprint_format_selection"),
		TEXT("blueprint_compile"),
		TEXT("blueprint_add_variable"),
	};

	return !MainAgentHiddenBlueprintTools.Contains(ToolId);
}
