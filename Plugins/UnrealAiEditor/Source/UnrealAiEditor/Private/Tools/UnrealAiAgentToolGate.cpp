#include "Tools/UnrealAiAgentToolGate.h"

#include "Context/AgentContextTypes.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Tools/UnrealAiProductSpecialistToolPolicy.h"
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
	// Plan DAG node workers keep the full Agent tool surface until plan-mode is rebuilt for specialists.
	if (Request.ThreadId.Contains(TEXT("_plan_")))
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

	if (Request.bBlueprintBuilderTurn)
	{
		TSet<FString> SurfaceTokens;
		bool bAllSurfaces = false;
		UnrealAiToolSurfaceCompatibility::ParseAgentSurfaces(*Def, SurfaceTokens, bAllSurfaces);
		return UnrealAiToolSurfaceCompatibility::ToolAllowedOnSurface(
			SurfaceTokens, bAllSurfaces, EUnrealAiToolSurfaceKind::BlueprintBuilder);
	}
	if (Request.ActiveProductSpecialistId != EUnrealAiProductSpecialistId::None)
	{
		return UnrealAiProductSpecialistToolPolicy::PassesSpecialistToolFilter(Request.ActiveProductSpecialistId, ToolId, *Def);
	}

	// Default main-agent turns should follow catalog-declared agent_surfaces.
	// This keeps builder-only mutators gated while allowing main-surface discovery tools.
	TSet<FString> SurfaceTokens;
	bool bAllSurfaces = false;
	UnrealAiToolSurfaceCompatibility::ParseAgentSurfaces(*Def, SurfaceTokens, bAllSurfaces);
	return UnrealAiToolSurfaceCompatibility::ToolAllowedOnSurface(
		SurfaceTokens, bAllSurfaces, EUnrealAiToolSurfaceKind::MainAgent);
}
