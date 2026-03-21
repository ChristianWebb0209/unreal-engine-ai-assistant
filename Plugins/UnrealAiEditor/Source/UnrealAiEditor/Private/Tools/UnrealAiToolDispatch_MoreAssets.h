#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetDelete(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_AssetDuplicate(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_AssetImport(const TSharedPtr<FJsonObject>& Args);
/** Opens any UObject asset in its default editor (Blueprint, MetaSound, Level Sequence, etc.). */
FUnrealAiToolInvocationResult UnrealAiDispatch_AssetOpenEditor(const TSharedPtr<FJsonObject>& Args);
