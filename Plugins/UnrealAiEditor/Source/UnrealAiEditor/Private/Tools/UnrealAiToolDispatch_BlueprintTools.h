#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintCompile(const TSharedPtr<FJsonObject>& Args);
/** Serialize a Blueprint graph to blueprint_apply_ir-compatible JSON (lossy for unknown node types). */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintExportIr(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintApplyIr(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintOpenGraphTab(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintAddVariable(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_AnimBlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args);
