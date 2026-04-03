#include "Tools/UnrealAiBlueprintToolGate.h"

#include "Context/AgentContextTypes.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Tools/UnrealAiToolCatalog.h"

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

bool UnrealAiBlueprintToolGate::PassesBuilderNarrowBm25Corpus(const FUnrealAiToolCatalog& Catalog, const FString& ToolId)
{
	static const TSet<FString> UtilityAllowlist = {
		TEXT("asset_registry_query"),
		TEXT("asset_index_fuzzy_search"),
		TEXT("asset_save_packages"),
		TEXT("asset_get_metadata"),
		TEXT("project_file_read_text"),
		TEXT("content_browser_navigate_folder"),
	};
	if (UtilityAllowlist.Contains(ToolId))
	{
		return true;
	}
	const TSharedPtr<FJsonObject> Def = Catalog.FindToolDefinition(ToolId);
	if (!Def.IsValid())
	{
		return false;
	}
	FString Cat;
	Def->TryGetStringField(TEXT("category"), Cat);
	if (Cat.Equals(TEXT("blueprints"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	const TSharedPtr<FJsonObject>* Ctx = nullptr;
	if (Def->TryGetObjectField(TEXT("context_selector"), Ctx) && Ctx && (*Ctx).IsValid())
	{
		bool bCore = false;
		(*Ctx)->TryGetBoolField(TEXT("blueprint_builder_core"), bCore);
		if (bCore)
		{
			return true;
		}
	}
	return false;
}
