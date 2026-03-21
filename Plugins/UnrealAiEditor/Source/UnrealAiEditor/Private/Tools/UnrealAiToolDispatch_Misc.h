#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_CollisionTraceEditorWorld(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_PhysicsImpulseActor(const TSharedPtr<FJsonObject>& Args);
