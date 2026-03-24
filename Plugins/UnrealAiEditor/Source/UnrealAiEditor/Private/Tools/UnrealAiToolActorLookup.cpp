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

AActor* UnrealAiResolveActorInWorld(UWorld* World, const FString& Spec)
{
	if (!World || Spec.IsEmpty())
	{
		return nullptr;
	}
	if (AActor* Exact = UnrealAiFindActorByPath(World, Spec))
	{
		return Exact;
	}
	// Loose matching: subpath, internal name, or label (models often omit /Temp/... prefix).
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A)
		{
			continue;
		}
		const FString& PN = A->GetPathName();
		if (PN.Equals(Spec, ESearchCase::IgnoreCase))
		{
			return A;
		}
		if (Spec.Len() >= 6 && PN.EndsWith(Spec, ESearchCase::IgnoreCase))
		{
			return A;
		}
		if (A->GetName().Equals(Spec, ESearchCase::IgnoreCase))
		{
			return A;
		}
		if (A->GetActorLabel().Equals(Spec, ESearchCase::IgnoreCase))
		{
			return A;
		}
	}
	return nullptr;
}
