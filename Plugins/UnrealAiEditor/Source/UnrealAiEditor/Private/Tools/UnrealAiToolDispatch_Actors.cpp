#include "Tools/UnrealAiToolDispatch_Actors.h"

#include "Tools/UnrealAiToolActorLookup.h"
#include "Tools/UnrealAiToolJson.h"

#include "Containers/Set.h"

#include "Components/PrimitiveComponent.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "UnrealEdGlobals.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealAiToolActorsInternal
{
	static FString NormalizeClassPathString(const FString& In)
	{
		FString P = In;
		P.TrimStartAndEndInline();
		// Asset path form: Class'/Script/Module.ClassName' -> /Script/Module.ClassName
		if (P.StartsWith(TEXT("Class'")))
		{
			P = P.Mid(6);
			if (P.Len() > 0 && P[P.Len() - 1] == TEXT('\''))
			{
				P.RemoveAt(P.Len() - 1, 1);
			}
		}
		return P;
	}

	static UClass* ResolveSpawnClass(const FString& ClassPathStr, FString& OutErr)
	{
		const FString P = NormalizeClassPathString(ClassPathStr);
		if (P.IsEmpty())
		{
			OutErr = TEXT("class_path is empty");
			return nullptr;
		}
		if (UClass* C = StaticLoadClass(UObject::StaticClass(), nullptr, *P))
		{
			return C;
		}
		const FSoftClassPath SoftPath(P);
		if (SoftPath.IsValid())
		{
			if (UClass* C = SoftPath.TryLoadClass<UObject>())
			{
				return C;
			}
		}
		OutErr = FString::Printf(
			TEXT("Could not resolve class_path (got '%s'). Use a path like /Game/MyActor.MyActor_C or /Script/Engine.StaticMeshActor"),
			*P);
		return nullptr;
	}

	static bool ParseVec3FromArray(const TArray<TSharedPtr<FJsonValue>>& Arr, FVector& Out)
	{
		if (Arr.Num() < 3)
		{
			return false;
		}
		double X = 0.0, Y = 0.0, Z = 0.0;
		Arr[0]->TryGetNumber(X);
		Arr[1]->TryGetNumber(Y);
		Arr[2]->TryGetNumber(Z);
		Out = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
		return true;
	}

	static bool ParseRotFromArray(const TArray<TSharedPtr<FJsonValue>>& Arr, FRotator& Out)
	{
		if (Arr.Num() < 3)
		{
			return false;
		}
		double P = 0.0, Yaw = 0.0, R = 0.0;
		Arr[0]->TryGetNumber(P);
		Arr[1]->TryGetNumber(Yaw);
		Arr[2]->TryGetNumber(R);
		Out = FRotator(static_cast<float>(P), static_cast<float>(Yaw), static_cast<float>(R));
		return true;
	}

	static bool ParseTransformJson(const TSharedPtr<FJsonObject>& T, FTransform& Out)
	{
		if (!T.IsValid())
		{
			return false;
		}
		FVector Loc(0.f, 0.f, 0.f);
		FRotator Rot(0.f, 0.f, 0.f);
		FVector Scale(1.f, 1.f, 1.f);
		const TArray<TSharedPtr<FJsonValue>>* LocArr = nullptr;
		if (T->TryGetArrayField(TEXT("location"), LocArr) && LocArr && ParseVec3FromArray(*LocArr, Loc))
		{
		}
		else
		{
			const TSharedPtr<FJsonObject>* LocObj = nullptr;
			if (T->TryGetObjectField(TEXT("location"), LocObj) && LocObj->IsValid())
			{
				double X = 0.0, Y = 0.0, Z = 0.0;
				(*LocObj)->TryGetNumberField(TEXT("x"), X);
				(*LocObj)->TryGetNumberField(TEXT("y"), Y);
				(*LocObj)->TryGetNumberField(TEXT("z"), Z);
				Loc = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* RotArr = nullptr;
		if (T->TryGetArrayField(TEXT("rotation"), RotArr) && RotArr && ParseRotFromArray(*RotArr, Rot))
		{
		}
		else
		{
			const TSharedPtr<FJsonObject>* RotObj = nullptr;
			if (T->TryGetObjectField(TEXT("rotation"), RotObj) && RotObj->IsValid())
			{
				double P = 0.0, Yaw = 0.0, R = 0.0;
				(*RotObj)->TryGetNumberField(TEXT("pitch"), P);
				(*RotObj)->TryGetNumberField(TEXT("yaw"), Yaw);
				(*RotObj)->TryGetNumberField(TEXT("roll"), R);
				Rot = FRotator(static_cast<float>(P), static_cast<float>(Yaw), static_cast<float>(R));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* ScaleArr = nullptr;
		if (T->TryGetArrayField(TEXT("scale"), ScaleArr) && ScaleArr && ParseVec3FromArray(*ScaleArr, Scale))
		{
		}
		else
		{
			const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
			if (T->TryGetObjectField(TEXT("scale"), ScaleObj) && ScaleObj->IsValid())
			{
				double X = 1.0, Y = 1.0, Z = 1.0;
				(*ScaleObj)->TryGetNumberField(TEXT("x"), X);
				(*ScaleObj)->TryGetNumberField(TEXT("y"), Y);
				(*ScaleObj)->TryGetNumberField(TEXT("z"), Z);
				Scale = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
			}
		}
		Out = FTransform(Rot, Loc, Scale);
		return true;
	}

	static bool IsLooseDestructiveConfirm(const FString& Confirm)
	{
		FString C = Confirm;
		C.TrimStartAndEndInline();
		if (C.IsEmpty())
		{
			return false;
		}
		if (C.Equals(TEXT("DELETE"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (C.Equals(TEXT("yes"), ESearchCase::IgnoreCase) //
			|| C.Equals(TEXT("y"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (C.Equals(TEXT("true"), ESearchCase::IgnoreCase) //
			|| C.Equals(TEXT("confirm"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (C == TEXT("1"))
		{
			return true;
		}
		return false;
	}

	static bool ActorMatchesLabelOrPath(AActor* A, const FString& Label)
	{
		if (!A || Label.IsEmpty())
		{
			return false;
		}
		if (A->GetActorLabel().Contains(Label, ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (A->GetName().Contains(Label, ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (A->GetPathName().Contains(Label, ESearchCase::IgnoreCase))
		{
			return true;
		}
		return false;
	}

	/** Build transform from `transform` object, or top-level location[]/rotation[]/scale[], or current actor pose. */
	static bool ResolveSetTransformFromArgs(const TSharedPtr<FJsonObject>& Args, AActor* A, FTransform& OutNew, FString& OutErr)
	{
		OutErr.Reset();
		if (!Args.IsValid() || !A)
		{
			OutErr = TEXT("internal: bad args or actor");
			return false;
		}
		const TSharedPtr<FJsonObject>* TObj = nullptr;
		if (Args->TryGetObjectField(TEXT("transform"), TObj) && TObj->IsValid())
		{
			if (!ParseTransformJson(*TObj, OutNew))
			{
				OutErr = TEXT("Invalid transform");
				return false;
			}
			return true;
		}
		const FTransform Cur = A->GetActorTransform();
		FVector Loc = Cur.GetLocation();
		FRotator Rot = Cur.Rotator();
		FVector Scale = Cur.GetScale3D();
		const TArray<TSharedPtr<FJsonValue>>* LocArr = nullptr;
		if (Args->TryGetArrayField(TEXT("location"), LocArr) && LocArr)
		{
			if (!ParseVec3FromArray(*LocArr, Loc))
			{
				OutErr = TEXT("location must be a length-3 number array [x,y,z]");
				return false;
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* RotArr = nullptr;
		if (Args->TryGetArrayField(TEXT("rotation"), RotArr) && RotArr)
		{
			if (!ParseRotFromArray(*RotArr, Rot))
			{
				OutErr = TEXT("rotation must be a length-3 number array [pitch,yaw,roll]");
				return false;
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* ScaleArr = nullptr;
		if (Args->TryGetArrayField(TEXT("scale"), ScaleArr) && ScaleArr)
		{
			if (!ParseVec3FromArray(*ScaleArr, Scale))
			{
				OutErr = TEXT("scale must be a length-3 number array [x,y,z]");
				return false;
			}
		}
		OutNew = FTransform(Rot, Loc, Scale);
		return true;
	}
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ActorDestroy(const TSharedPtr<FJsonObject>& Args)
{
	FString ActorPath;
	FString Confirm;
	if (!Args->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("actor_path is required"));
	}
	Args->TryGetStringField(TEXT("confirm"), Confirm);
	if (!UnrealAiToolActorsInternal::IsLooseDestructiveConfirm(Confirm))
	{
		return UnrealAiToolJson::Error(TEXT("confirm must show intent to delete: DELETE, yes, true, or y."));
	}
	UWorld* World = UnrealAiGetEditorWorld();
	AActor* A = UnrealAiResolveActorInWorld(World, ActorPath);
	if (!A)
	{
		return UnrealAiToolJson::Error(TEXT("Actor not found"));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnActorDestroy", "Unreal AI: destroy actor"));
	if (World)
	{
		World->DestroyActor(A);
	}
	else
	{
		A->Destroy();
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("destroyed_path"), ActorPath);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ActorFindByLabel(const TSharedPtr<FJsonObject>& Args)
{
	UWorld* World = UnrealAiGetEditorWorld();
	if (!World)
	{
		return UnrealAiToolJson::Error(TEXT("No editor world"));
	}
	FString Label;
	FString Tag;
	Args->TryGetStringField(TEXT("label"), Label);
	Args->TryGetStringField(TEXT("tag"), Tag);
	if (Label.IsEmpty() && Tag.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("Provide label and/or tag"));
	}
	if (!Label.IsEmpty() && Tag.IsEmpty())
	{
		if (AActor* Direct = UnrealAiResolveActorInWorld(World, Label))
		{
			TArray<TSharedPtr<FJsonValue>> One;
			One.Add(MakeShareable(new FJsonValueString(Direct->GetPathName())));
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("ok"), true);
			O->SetArrayField(TEXT("actor_paths"), One);
			O->SetNumberField(TEXT("count"), 1.0);
			return UnrealAiToolJson::Ok(O);
		}
	}
	TArray<TSharedPtr<FJsonValue>> Paths;
	TSet<FString> Seen;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A)
		{
			continue;
		}
		bool bMatch = true;
		if (!Label.IsEmpty() && !UnrealAiToolActorsInternal::ActorMatchesLabelOrPath(A, Label))
		{
			bMatch = false;
		}
		if (bMatch && !Tag.IsEmpty())
		{
			bool bTagHit = false;
			for (const FName& Tg : A->Tags)
			{
				if (Tg.ToString().Contains(Tag))
				{
					bTagHit = true;
					break;
				}
			}
			if (!bTagHit)
			{
				bMatch = false;
			}
		}
		if (bMatch)
		{
			const FString Pn = A->GetPathName();
			if (!Seen.Contains(Pn))
			{
				Seen.Add(Pn);
				Paths.Add(MakeShareable(new FJsonValueString(Pn)));
			}
		}
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetArrayField(TEXT("actor_paths"), Paths);
	O->SetNumberField(TEXT("count"), static_cast<double>(Paths.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ActorGetTransform(const TSharedPtr<FJsonObject>& Args)
{
	FString ActorPath;
	if (!Args->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("actor_path is required"));
	}
	UWorld* World = UnrealAiGetEditorWorld();
	AActor* A = UnrealAiResolveActorInWorld(World, ActorPath);
	if (!A)
	{
		return UnrealAiToolJson::Error(TEXT("Actor not found"));
	}
	const FTransform T = A->GetActorTransform();
	const FVector Loc = T.GetLocation();
	const FRotator Rot = T.Rotator();
	const FVector Scale = T.GetScale3D();
	TSharedPtr<FJsonObject> LocO = MakeShared<FJsonObject>();
	LocO->SetNumberField(TEXT("x"), Loc.X);
	LocO->SetNumberField(TEXT("y"), Loc.Y);
	LocO->SetNumberField(TEXT("z"), Loc.Z);
	TSharedPtr<FJsonObject> RotO = MakeShared<FJsonObject>();
	RotO->SetNumberField(TEXT("pitch"), Rot.Pitch);
	RotO->SetNumberField(TEXT("yaw"), Rot.Yaw);
	RotO->SetNumberField(TEXT("roll"), Rot.Roll);
	TSharedPtr<FJsonObject> ScO = MakeShared<FJsonObject>();
	ScO->SetNumberField(TEXT("x"), Scale.X);
	ScO->SetNumberField(TEXT("y"), Scale.Y);
	ScO->SetNumberField(TEXT("z"), Scale.Z);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetObjectField(TEXT("location"), LocO);
	O->SetObjectField(TEXT("rotation"), RotO);
	O->SetObjectField(TEXT("scale"), ScO);
	if (const UPrimitiveComponent* Pc = Cast<UPrimitiveComponent>(A->GetRootComponent()))
	{
		switch (Pc->Mobility)
		{
		case EComponentMobility::Static:
			O->SetStringField(TEXT("mobility"), TEXT("Static"));
			break;
		case EComponentMobility::Stationary:
			O->SetStringField(TEXT("mobility"), TEXT("Stationary"));
			break;
		default:
			O->SetStringField(TEXT("mobility"), TEXT("Movable"));
			break;
		}
	}
	else
	{
		O->SetStringField(TEXT("mobility"), TEXT("unknown"));
	}
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ActorSetTransform(const TSharedPtr<FJsonObject>& Args)
{
	FString ActorPath;
	if (!Args->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("actor_path is required"));
	}
	bool bTeleportPhysics = true;
	Args->TryGetBoolField(TEXT("teleport_physics"), bTeleportPhysics);
	UWorld* World = UnrealAiGetEditorWorld();
	AActor* A = UnrealAiResolveActorInWorld(World, ActorPath);
	if (!A)
	{
		return UnrealAiToolJson::Error(TEXT("Actor not found"));
	}
	FTransform NewT;
	FString TfErr;
	if (!UnrealAiToolActorsInternal::ResolveSetTransformFromArgs(Args, A, NewT, TfErr))
	{
		return UnrealAiToolJson::Error(TfErr.IsEmpty() ? TEXT("Invalid transform arguments") : TfErr);
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnActorSetXform", "Unreal AI: set transform"));
	A->SetActorTransform(
		NewT,
		false,
		nullptr,
		bTeleportPhysics ? ETeleportType::TeleportPhysics : ETeleportType::None);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("actor_path"), A->GetPathName());
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ActorSetVisibility(const TSharedPtr<FJsonObject>& Args)
{
	FString ActorPath;
	if (!Args->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("actor_path is required"));
	}
	bool bHidden = false;
	Args->TryGetBoolField(TEXT("hidden"), bHidden);
	UWorld* World = UnrealAiGetEditorWorld();
	AActor* A = UnrealAiResolveActorInWorld(World, ActorPath);
	if (!A)
	{
		return UnrealAiToolJson::Error(TEXT("Actor not found"));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnActorVis", "Unreal AI: set visibility"));
	A->SetIsTemporarilyHiddenInEditor(bHidden);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetBoolField(TEXT("hidden"), bHidden);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ActorAttachTo(const TSharedPtr<FJsonObject>& Args)
{
	FString ChildPath;
	FString ParentPath;
	if (!Args->TryGetStringField(TEXT("child_path"), ChildPath) || ChildPath.IsEmpty()
		|| !Args->TryGetStringField(TEXT("parent_path"), ParentPath) || ParentPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("child_path and parent_path are required"));
	}
	FString Socket;
	Args->TryGetStringField(TEXT("socket"), Socket);
	UWorld* World = UnrealAiGetEditorWorld();
	AActor* Child = UnrealAiResolveActorInWorld(World, ChildPath);
	AActor* Parent = UnrealAiResolveActorInWorld(World, ParentPath);
	if (!Child || !Parent)
	{
		return UnrealAiToolJson::Error(TEXT("Child or parent actor not found"));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnAttach", "Unreal AI: attach actor"));
	const FAttachmentTransformRules Rules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
	Child->AttachToActor(Parent, Rules, Socket.IsEmpty() ? NAME_None : FName(*Socket));
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ActorSpawnFromClass(const TSharedPtr<FJsonObject>& Args)
{
	FString ClassPath;
	const TArray<TSharedPtr<FJsonValue>>* LocArr = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* RotArr = nullptr;
	if (!Args->TryGetStringField(TEXT("class_path"), ClassPath) || ClassPath.IsEmpty()
		|| !Args->TryGetArrayField(TEXT("location"), LocArr) || !LocArr
		|| !Args->TryGetArrayField(TEXT("rotation"), RotArr) || !RotArr)
	{
		return UnrealAiToolJson::Error(TEXT("class_path, location[], rotation[] are required"));
	}
	FVector Loc = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;
	if (!UnrealAiToolActorsInternal::ParseVec3FromArray(*LocArr, Loc)
		|| !UnrealAiToolActorsInternal::ParseRotFromArray(*RotArr, Rot))
	{
		return UnrealAiToolJson::Error(TEXT("location and rotation must be length-3 number arrays"));
	}
	UWorld* World = UnrealAiGetEditorWorld();
	if (!World)
	{
		return UnrealAiToolJson::Error(TEXT("No editor world"));
	}
	FString ResolveErr;
	UClass* Class = UnrealAiToolActorsInternal::ResolveSpawnClass(ClassPath, ResolveErr);
	if (!Class)
	{
		return UnrealAiToolJson::Error(ResolveErr.IsEmpty()
			? TEXT("Could not resolve class_path (use e.g. /Game/BP_MyActor.BP_MyActor_C)")
			: ResolveErr);
	}
	if (!Class->IsChildOf(AActor::StaticClass()))
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("'%s' is not an Actor class — only AActor subclasses can be spawned in the level (e.g. /Script/Engine.StaticMeshActor, "
				 "/Script/Engine.PointLight, or a Blueprint actor). SceneComponent and other non-actor types cannot be spawned here."),
			*Class->GetName()));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnSpawn", "Unreal AI: spawn actor"));
	FActorSpawnParameters Sp;
	const FTransform Xform(Rot, Loc);
	AActor* A = World->SpawnActor(Class, &Xform, Sp);
	if (!A)
	{
		return UnrealAiToolJson::Error(TEXT("SpawnActor failed"));
	}
	FString Folder;
	if (Args->TryGetStringField(TEXT("folder_path"), Folder) && !Folder.IsEmpty())
	{
		A->SetFolderPath(FName(*Folder));
	}
	FString ActorLabel;
	if (!Args->TryGetStringField(TEXT("label"), ActorLabel) || ActorLabel.IsEmpty())
	{
		Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	}
	if (!ActorLabel.IsEmpty())
	{
		A->SetActorLabel(ActorLabel, true);
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("actor_path"), A->GetPathName());
	return UnrealAiToolJson::Ok(O);
}
