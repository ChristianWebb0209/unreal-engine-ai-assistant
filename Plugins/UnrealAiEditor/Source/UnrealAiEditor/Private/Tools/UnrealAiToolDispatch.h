#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

class FUnrealAiBackendRegistry;

/** Routes tool_id to UE5 editor implementations (game thread). */
FUnrealAiToolInvocationResult UnrealAiDispatchTool(
	const FString& ToolId,
	const TSharedPtr<FJsonObject>& Args,
	const TSharedPtr<FJsonObject>& CatalogEntry,
	FUnrealAiBackendRegistry* Registry,
	const FString& SessionProjectId,
	const FString& SessionThreadId);
