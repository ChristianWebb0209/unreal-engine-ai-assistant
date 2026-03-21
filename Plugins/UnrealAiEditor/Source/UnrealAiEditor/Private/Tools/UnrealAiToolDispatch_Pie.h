#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_PieStart(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_PieStop(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_PieStatus(const TSharedPtr<FJsonObject>& Args);
