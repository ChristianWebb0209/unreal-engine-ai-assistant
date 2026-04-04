#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

/** Environment Builder surface tools (PCG / landscape / foliage); returns true if ToolId was handled. */
bool UnrealAiTryDispatchEnvironmentBuilderTool(
	const FString& ToolId,
	const TSharedPtr<FJsonObject>& Args,
	FUnrealAiToolInvocationResult& OutResult);

FUnrealAiToolInvocationResult UnrealAiDispatch_FoliagePaintInstances(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_LandscapeImportHeightmap(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_PcgGenerate(const TSharedPtr<FJsonObject>& Args);
