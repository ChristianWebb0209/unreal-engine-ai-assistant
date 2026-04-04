#include "Tools/UnrealAiBlueprintToolGate.h"

#include "Context/AgentContextTypes.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Tools/UnrealAiToolSurfaceCompatibility.h"

bool UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(
	const FUnrealAiAgentTurnRequest& Request,
	const FString& ToolId,
	const FUnrealAiToolCatalog* CatalogOpt)
{
	if (Request.Mode != EUnrealAiAgentMode::Agent)
	{
		return true;
	}
	if (!Request.bOmitMainAgentBlueprintMutationTools)
	{
		return true;
	}
	if (!CatalogOpt)
	{
		return true;
	}

	const TSharedPtr<FJsonObject> Def = CatalogOpt->FindToolDefinition(ToolId);
	if (!Def.IsValid())
	{
		return true;
	}

	TSet<FString> SurfaceTokens;
	bool bAllSurfaces = false;
	UnrealAiToolSurfaceCompatibility::ParseAgentSurfaces(*Def, SurfaceTokens, bAllSurfaces);

	const EUnrealAiToolSurfaceKind Kind = Request.bBlueprintBuilderTurn ? EUnrealAiToolSurfaceKind::BlueprintBuilder
																		 : EUnrealAiToolSurfaceKind::MainAgent;

	return UnrealAiToolSurfaceCompatibility::ToolAllowedOnSurface(SurfaceTokens, bAllSurfaces, Kind);
}
