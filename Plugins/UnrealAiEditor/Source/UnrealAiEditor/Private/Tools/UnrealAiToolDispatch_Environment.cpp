#include "Tools/UnrealAiToolDispatch_Environment.h"

#include "Tools/UnrealAiToolJson.h"

bool UnrealAiTryDispatchEnvironmentBuilderTool(
	const FString& ToolId,
	const TSharedPtr<FJsonObject>& Args,
	FUnrealAiToolInvocationResult& OutResult)
{
	if (ToolId == TEXT("foliage_paint_instances"))
	{
		OutResult = UnrealAiDispatch_FoliagePaintInstances(Args);
		return true;
	}
	if (ToolId == TEXT("landscape_import_heightmap"))
	{
		OutResult = UnrealAiDispatch_LandscapeImportHeightmap(Args);
		return true;
	}
	if (ToolId == TEXT("pcg_generate"))
	{
		OutResult = UnrealAiDispatch_PcgGenerate(Args);
		return true;
	}
	return false;
}

FUnrealAiToolInvocationResult UnrealAiDispatch_FoliagePaintInstances(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	return UnrealAiToolJson::Error(
		TEXT("foliage_paint_instances: automated foliage painting is not implemented in this plugin build; use editor foliage mode."));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_LandscapeImportHeightmap(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	return UnrealAiToolJson::Error(
		TEXT("landscape_import_heightmap: use the Landscape editor import UI; no headless API wired in this build."));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_PcgGenerate(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	return UnrealAiToolJson::Error(TEXT("pcg_generate: requires PCG component context; not implemented in this plugin build."));
}
