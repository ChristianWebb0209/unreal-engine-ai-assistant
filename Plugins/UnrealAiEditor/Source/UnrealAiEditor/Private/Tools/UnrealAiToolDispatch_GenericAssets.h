#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

/** Create arbitrary assets under /Game via IAssetTools (class + optional factory). */
FUnrealAiToolInvocationResult UnrealAiDispatch_AssetCreate(const TSharedPtr<FJsonObject>& Args);
/** Export UObject editable properties to JSON (reflection / ExportText). */
FUnrealAiToolInvocationResult UnrealAiDispatch_AssetExportProperties(const TSharedPtr<FJsonObject>& Args);
/** Apply property deltas via reflection / ImportText (dry_run supported). */
FUnrealAiToolInvocationResult UnrealAiDispatch_AssetApplyProperties(const TSharedPtr<FJsonObject>& Args);
/** Referencers of an asset (Asset Registry). */
FUnrealAiToolInvocationResult UnrealAiDispatch_AssetFindReferencers(const TSharedPtr<FJsonObject>& Args);
/** Direct dependencies of an asset (Asset Registry). */
FUnrealAiToolInvocationResult UnrealAiDispatch_AssetGetDependencies(const TSharedPtr<FJsonObject>& Args);

/** Create a Level Sequence asset under /Game (no extra factory). */
FUnrealAiToolInvocationResult UnrealAiDispatch_LevelSequenceCreateAsset(const TSharedPtr<FJsonObject>& Args);
