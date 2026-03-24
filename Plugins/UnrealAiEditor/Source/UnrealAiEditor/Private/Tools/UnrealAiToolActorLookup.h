#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/** Resolve actors in the current editor world by full object path (from editor_get_selection / scene tools). */
UWorld* UnrealAiGetEditorWorld();

AActor* UnrealAiFindActorByPath(UWorld* World, const FString& ActorPath);

/**
 * Resolve an actor by full path first, then by path suffix / short name / label-style matches.
 * Use when the model omits the outer package path or pastes only the PersistentLevel.* segment.
 */
AActor* UnrealAiResolveActorInWorld(UWorld* World, const FString& Spec);
