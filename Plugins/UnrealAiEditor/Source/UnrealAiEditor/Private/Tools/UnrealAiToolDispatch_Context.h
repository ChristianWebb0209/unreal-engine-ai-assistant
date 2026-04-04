#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

class FUnrealAiBackendRegistry;

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetRegistryQuery(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_EditorStateSnapshotRead(
	FUnrealAiBackendRegistry* Registry,
	const FString& ProjectId,
	const FString& ThreadId,
	const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_ContentBrowserSyncAsset(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_EngineMessageLogRead(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_ToolAuditAppend(const TSharedPtr<FJsonObject>& Args);
