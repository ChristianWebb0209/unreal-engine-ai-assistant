#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

/**
 * Canonicalize Blueprint object paths: /Game/Folder/Asset -> /Game/Folder/Asset.Asset
 * (Unreal requires PackagePath.ObjectName for LoadObject).
 */
void UnrealAiNormalizeBlueprintObjectPath(FString& BlueprintObjectPath);

/** Open the Blueprint editor; optionally focus a graph by name (EventGraph, etc.). */
void UnrealAiFocusBlueprintEditor(const FString& BlueprintObjectPath, const FString& GraphName);

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintCompile(const TSharedPtr<FJsonObject>& Args);
/** Serialize a Blueprint graph to blueprint_apply_ir-compatible JSON (lossy for unknown node types). */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintExportIr(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintApplyIr(const TSharedPtr<FJsonObject>& Args);
/** Run UnrealBlueprintFormatter LayoutEntireGraph on one script graph (default: first ubergraph). */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintFormatGraph(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintOpenGraphTab(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintAddVariable(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_AnimBlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args);
