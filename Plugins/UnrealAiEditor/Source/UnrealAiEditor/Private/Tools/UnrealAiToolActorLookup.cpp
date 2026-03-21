#include "Tools/UnrealAiToolActorLookup.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UnrealEdGlobals.h"

UWorld* UnrealAiGetEditorWorld()
{
	if (!GEditor)
	{
		return nullptr;
	}
	return GEditor->GetEditorWorldContext().World();
}

AActor* UnrealAiFindActorByPath(UWorld* World, const FString& ActorPath)
{
	if (!World || ActorPath.IsEmpty())
	{
		return nullptr;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A && A->GetPathName() == ActorPath)
		{
			return A;
		}
	}
	return nullptr;
}
