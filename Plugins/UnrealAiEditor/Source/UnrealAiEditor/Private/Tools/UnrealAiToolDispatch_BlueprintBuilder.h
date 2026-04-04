#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

/** Blueprint Builder surface tools (K2 graph + material graph); returns true if ToolId was handled. */
bool UnrealAiTryDispatchBlueprintBuilderSurfaceTool(
	const FString& ToolId,
	const TSharedPtr<FJsonObject>& Args,
	FUnrealAiToolInvocationResult& OutResult);
