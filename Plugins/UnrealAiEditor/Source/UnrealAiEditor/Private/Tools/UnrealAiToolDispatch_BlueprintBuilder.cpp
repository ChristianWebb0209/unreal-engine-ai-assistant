#include "Tools/UnrealAiToolDispatch_BlueprintBuilder.h"

#include "Tools/UnrealAiToolDispatch_AssetsMaterials.h"
#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"
#include "Tools/UnrealAiToolDispatch_MaterialGraph.h"

bool UnrealAiTryDispatchBlueprintBuilderSurfaceTool(
	const FString& ToolId,
	const TSharedPtr<FJsonObject>& Args,
	FUnrealAiToolInvocationResult& OutResult)
{
	if (ToolId == TEXT("material_graph_summarize"))
	{
		OutResult = UnrealAiDispatch_MaterialGraphSummarize(Args);
		return true;
	}
	if (ToolId == TEXT("material_graph_export"))
	{
		OutResult = UnrealAiDispatch_MaterialGraphExport(Args);
		return true;
	}
	if (ToolId == TEXT("material_graph_patch"))
	{
		OutResult = UnrealAiDispatch_MaterialGraphPatch(Args);
		return true;
	}
	if (ToolId == TEXT("material_graph_compile"))
	{
		OutResult = UnrealAiDispatch_MaterialGraphCompile(Args);
		return true;
	}
	if (ToolId == TEXT("material_graph_validate"))
	{
		OutResult = UnrealAiDispatch_MaterialGraphValidate(Args);
		return true;
	}

	if (ToolId == TEXT("blueprint_compile"))
	{
		OutResult = UnrealAiDispatch_BlueprintCompile(Args);
		return true;
	}
	if (ToolId == TEXT("blueprint_set_component_default"))
	{
		OutResult = UnrealAiDispatch_BlueprintSetComponentDefault(Args);
		return true;
	}
	if (ToolId == TEXT("blueprint_graph_patch"))
	{
		OutResult = UnrealAiDispatch_BlueprintGraphPatch(Args);
		return true;
	}
	if (ToolId == TEXT("blueprint_graph_list_pins"))
	{
		OutResult = UnrealAiDispatch_BlueprintGraphListPins(Args);
		return true;
	}
	if (ToolId == TEXT("blueprint_format_graph"))
	{
		OutResult = UnrealAiDispatch_BlueprintFormatGraph(Args);
		return true;
	}
	if (ToolId == TEXT("blueprint_get_graph_summary"))
	{
		OutResult = UnrealAiDispatch_BlueprintGetGraphSummary(Args);
		return true;
	}
	if (ToolId == TEXT("blueprint_graph_introspect"))
	{
		OutResult = UnrealAiDispatch_BlueprintGraphIntrospect(Args);
		return true;
	}
	if (ToolId == TEXT("blueprint_verify_graph"))
	{
		OutResult = UnrealAiDispatch_BlueprintVerifyGraph(Args);
		return true;
	}

	return false;
}
