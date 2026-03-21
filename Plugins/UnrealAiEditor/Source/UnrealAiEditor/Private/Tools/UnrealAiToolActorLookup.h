#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/** Resolve actors in the current editor world by full object path (from editor_get_selection / scene tools). */
UWorld* UnrealAiGetEditorWorld();

AActor* UnrealAiFindActorByPath(UWorld* World, const FString& ActorPath);
