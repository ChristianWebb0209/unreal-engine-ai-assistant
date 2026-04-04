#include "Tools/UnrealAiAgentToolGate.h"

#include "Context/AgentContextTypes.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Tools/UnrealAiToolSurfaceCompatibility.h"

bool UnrealAiAgentToolGate::PassesToolSurfaceFilter(
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

	EUnrealAiToolSurfaceKind Kind = EUnrealAiToolSurfaceKind::MainAgent;
	if (Request.bEnvironmentBuilderTurn)
	{
		Kind = EUnrealAiToolSurfaceKind::EnvironmentBuilder;
	}
	else if (Request.bBlueprintBuilderTurn)
	{
		Kind = EUnrealAiToolSurfaceKind::BlueprintBuilder;
	}

	return UnrealAiToolSurfaceCompatibility::ToolAllowedOnSurface(SurfaceTokens, bAllSurfaces, Kind);
}
