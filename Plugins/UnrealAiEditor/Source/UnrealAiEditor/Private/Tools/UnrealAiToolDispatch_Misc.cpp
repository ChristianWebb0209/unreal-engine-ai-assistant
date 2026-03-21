#include "Tools/UnrealAiToolDispatch_Misc.h"

#include "Tools/UnrealAiToolActorLookup.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/UnrealAiToolJsonGeom.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_CollisionTraceEditorWorld(const TSharedPtr<FJsonObject>& Args)
{
	UWorld* World = UnrealAiGetEditorWorld();
	if (!World)
	{
		return UnrealAiToolJson::Error(TEXT("No editor world"));
	}
	const TArray<TSharedPtr<FJsonValue>>* Sa = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Ea = nullptr;
	if (!Args->TryGetArrayField(TEXT("start"), Sa) || !Sa || !Args->TryGetArrayField(TEXT("end"), Ea) || !Ea)
	{
		return UnrealAiToolJson::Error(TEXT("start and end [x,y,z] arrays are required"));
	}
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	if (!UnrealAiParseVec3NumberArray(*Sa, Start) || !UnrealAiParseVec3NumberArray(*Ea, End))
	{
		return UnrealAiToolJson::Error(TEXT("start/end must be length-3 number arrays"));
	}
	FHitResult Hit;
	FCollisionQueryParams P(SCENE_QUERY_STAT(UnrealAiToolTrace), false);
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, P);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetBoolField(TEXT("hit"), bHit);
	if (bHit)
	{
		O->SetStringField(TEXT("hit_actor_path"), Hit.GetActor() ? Hit.GetActor()->GetPathName() : FString());
		FVector Loc = Hit.ImpactPoint;
		TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
		L->SetNumberField(TEXT("x"), Loc.X);
		L->SetNumberField(TEXT("y"), Loc.Y);
		L->SetNumberField(TEXT("z"), Loc.Z);
		O->SetObjectField(TEXT("impact_point"), L);
	}
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_PhysicsImpulseActor(const TSharedPtr<FJsonObject>& Args)
{
	FString ActorPath;
	if (!Args->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("actor_path is required"));
	}
	FVector Impulse = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* Io = nullptr;
	if (Args->TryGetObjectField(TEXT("impulse"), Io) && Io->IsValid())
	{
		double X = 0.0, Y = 0.0, Z = 0.0;
		(*Io)->TryGetNumberField(TEXT("x"), X);
		(*Io)->TryGetNumberField(TEXT("y"), Y);
		(*Io)->TryGetNumberField(TEXT("z"), Z);
		Impulse = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
	}
	UWorld* World = UnrealAiGetEditorWorld();
	AActor* A = UnrealAiFindActorByPath(World, ActorPath);
	if (!A)
	{
		return UnrealAiToolJson::Error(TEXT("Actor not found"));
	}
	UPrimitiveComponent* Pc = Cast<UPrimitiveComponent>(A->GetRootComponent());
	if (!Pc)
	{
		return UnrealAiToolJson::Error(TEXT("Actor has no primitive root component"));
	}
	Pc->AddImpulse(Impulse, NAME_None, true);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}
