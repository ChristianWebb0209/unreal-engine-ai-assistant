#include "Tools/UnrealAiToolDispatch_PcgEnvironment.h"

#include "Tools/UnrealAiToolJson.h"

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
