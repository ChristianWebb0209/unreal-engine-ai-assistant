#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_ActorDestroy(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_ActorFindByLabel(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_ActorGetTransform(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_ActorSetTransform(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_ActorGetVisibility(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_ActorSetVisibility(const TSharedPtr<FJsonObject>& Args);
/** Flips editor temporary hide flag; alias name matches common model/tooling expectations. */
FUnrealAiToolInvocationResult UnrealAiDispatch_ActorBlueprintToggleVisibility(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_ActorAttachTo(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_ActorSpawnFromClass(const TSharedPtr<FJsonObject>& Args);
