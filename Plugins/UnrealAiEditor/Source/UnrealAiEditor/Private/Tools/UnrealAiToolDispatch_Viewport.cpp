#include "Tools/UnrealAiToolDispatch_Viewport.h"

#include "Tools/UnrealAiToolActorLookup.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/UnrealAiToolViewportHelpers.h"

#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Selection.h"
#include "UnrealClient.h"
#include "UnrealEdGlobals.h"

static FEditorViewportClient* GetVc()
{
	return UnrealAiGetActiveLevelViewportClient();
}

namespace UnrealAiViewportFrameInternal
{
	static constexpr float MinFallbackExtentUU = 128.f;

	static void AddActorBounds(FBox& Bounds, AActor* A)
	{
		if (!A)
		{
			return;
		}
		FBox Piece;
		const FBox CompBox = A->GetComponentsBoundingBox();
		if (CompBox.IsValid && !CompBox.GetExtent().IsNearlyZero(1.f))
		{
			Piece = CompBox;
		}
		else
		{
			Piece = FBox::BuildAABB(A->GetActorLocation(), FVector(MinFallbackExtentUU));
		}
		if (!Bounds.IsValid)
		{
			Bounds = Piece;
		}
		else
		{
			Bounds += Piece;
		}
	}
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportCameraGetTransform()
{
	FEditorViewportClient* VC = GetVc();
	if (!VC)
	{
		return UnrealAiToolJson::Error(TEXT("No active editor viewport"));
	}
	const FVector Loc = VC->GetViewLocation();
	const FRotator Rot = VC->GetViewRotation();
	const float Fov = VC->ViewFOV;
	FIntPoint Size = GEditor && GEditor->GetActiveViewport() ? GEditor->GetActiveViewport()->GetSizeXY() : FIntPoint::ZeroValue;
	TSharedPtr<FJsonObject> LocO = MakeShared<FJsonObject>();
	LocO->SetNumberField(TEXT("x"), Loc.X);
	LocO->SetNumberField(TEXT("y"), Loc.Y);
	LocO->SetNumberField(TEXT("z"), Loc.Z);
	TSharedPtr<FJsonObject> RotO = MakeShared<FJsonObject>();
	RotO->SetNumberField(TEXT("pitch"), Rot.Pitch);
	RotO->SetNumberField(TEXT("yaw"), Rot.Yaw);
	RotO->SetNumberField(TEXT("roll"), Rot.Roll);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetObjectField(TEXT("location"), LocO);
	O->SetObjectField(TEXT("rotation"), RotO);
	O->SetNumberField(TEXT("fov"), static_cast<double>(Fov));
	O->SetNumberField(TEXT("viewport_width"), static_cast<double>(Size.X));
	O->SetNumberField(TEXT("viewport_height"), static_cast<double>(Size.Y));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportCameraSetTransform(const TSharedPtr<FJsonObject>& Args)
{
	FEditorViewportClient* VC = GetVc();
	if (!VC)
	{
		return UnrealAiToolJson::Error(TEXT("No active editor viewport"));
	}
	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	Args->TryGetObjectField(TEXT("location"), LocObj);
	Args->TryGetObjectField(TEXT("rotation"), RotObj);
	if (LocObj && LocObj->IsValid())
	{
		double X = 0.0, Y = 0.0, Z = 0.0;
		(*LocObj)->TryGetNumberField(TEXT("x"), X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Z);
		VC->SetViewLocation(FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z)));
	}
	if (RotObj && RotObj->IsValid())
	{
		double P = 0.0, Yaw = 0.0, R = 0.0;
		(*RotObj)->TryGetNumberField(TEXT("pitch"), P);
		(*RotObj)->TryGetNumberField(TEXT("yaw"), Yaw);
		(*RotObj)->TryGetNumberField(TEXT("roll"), R);
		VC->SetViewRotation(FRotator(static_cast<float>(P), static_cast<float>(Yaw), static_cast<float>(R)));
	}
	double Fov = 0.0;
	if (Args->TryGetNumberField(TEXT("fov"), Fov))
	{
		VC->ViewFOV = static_cast<float>(Fov);
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportCameraDolly(const TSharedPtr<FJsonObject>& Args)
{
	FEditorViewportClient* VC = GetVc();
	if (!VC)
	{
		return UnrealAiToolJson::Error(TEXT("No active editor viewport"));
	}
	double Delta = 0.0;
	if (!Args->TryGetNumberField(TEXT("distance"), Delta))
	{
		Args->TryGetNumberField(TEXT("delta"), Delta);
	}
	const FVector Loc = VC->GetViewLocation();
	const FRotator Rot = VC->GetViewRotation();
	const FVector Forward = Rot.Vector();
	VC->SetViewLocation(Loc + Forward * static_cast<float>(Delta));
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportCameraPan(const TSharedPtr<FJsonObject>& Args)
{
	FEditorViewportClient* VC = GetVc();
	if (!VC)
	{
		return UnrealAiToolJson::Error(TEXT("No active editor viewport"));
	}
	double Dx = 0.0, Dy = 0.0;
	Args->TryGetNumberField(TEXT("delta_right"), Dx);
	Args->TryGetNumberField(TEXT("delta_up"), Dy);
	const FVector Loc = VC->GetViewLocation();
	const FRotator Rot = VC->GetViewRotation();
	const FRotationMatrix Rm(Rot);
	const FVector Right = Rm.GetUnitAxis(EAxis::Y);
	const FVector Up = Rm.GetUnitAxis(EAxis::Z);
	VC->SetViewLocation(Loc + Right * static_cast<float>(Dx) + Up * static_cast<float>(Dy));
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportCameraOrbit(const TSharedPtr<FJsonObject>& Args)
{
	FEditorViewportClient* VC = GetVc();
	if (!VC)
	{
		return UnrealAiToolJson::Error(TEXT("No active editor viewport"));
	}
	double DPitch = 0.0, DYaw = 0.0;
	Args->TryGetNumberField(TEXT("delta_pitch"), DPitch);
	Args->TryGetNumberField(TEXT("delta_yaw"), DYaw);
	FRotator R = VC->GetViewRotation();
	R.Pitch += static_cast<float>(DPitch);
	R.Yaw += static_cast<float>(DYaw);
	VC->SetViewRotation(R);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportCameraPilot(const TSharedPtr<FJsonObject>& Args)
{
	FString ActorPath;
	if (!Args->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("actor_path is required"));
	}
	FEditorViewportClient* VC = GetVc();
	UWorld* World = UnrealAiGetEditorWorld();
	AActor* A = UnrealAiResolveActorInWorld(World, ActorPath);
	if (!VC || !A)
	{
		return UnrealAiToolJson::Error(TEXT("Viewport or actor not found"));
	}
	const FVector Target = A->GetActorLocation();
	const FVector Eye = Target + FVector(-400.f, 0.f, 200.f);
	VC->SetViewLocation(Eye);
	VC->SetViewRotation((Target - Eye).Rotation());
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("note"), TEXT("Pilot simplified: camera moved to face actor"));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportFrameActors(const TSharedPtr<FJsonObject>& Args)
{
	const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
	if (!Args->TryGetArrayField(TEXT("actor_paths"), Paths) || !Paths || Paths->Num() == 0)
	{
		return UnrealAiToolJson::Error(TEXT("actor_paths array is required"));
	}
	FEditorViewportClient* VC = GetVc();
	UWorld* World = UnrealAiGetEditorWorld();
	if (!VC || !World)
	{
		return UnrealAiToolJson::Error(TEXT("Viewport or world not available"));
	}
	FBox Bounds(ForceInit);
	for (const TSharedPtr<FJsonValue>& V : *Paths)
	{
		FString P;
		if (!V.IsValid() || !V->TryGetString(P))
		{
			continue;
		}
		if (AActor* A = UnrealAiResolveActorInWorld(World, P))
		{
			UnrealAiViewportFrameInternal::AddActorBounds(Bounds, A);
		}
	}
	if (!Bounds.IsValid || Bounds.GetExtent().IsNearlyZero(1.f))
	{
		return UnrealAiToolJson::Error(TEXT("No resolvable actors for framing"));
	}
	VC->FocusViewportOnBox(Bounds, true);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportFrameSelection(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("GEditor not available"));
	}
	FEditorViewportClient* VC = GetVc();
	USelection* Sel = GEditor->GetSelectedActors();
	if (!VC || !Sel)
	{
		return UnrealAiToolJson::Error(TEXT("Viewport or selection not available"));
	}
	FBox Bounds(ForceInit);
	for (FSelectionIterator It(*Sel); It; ++It)
	{
		if (AActor* A = Cast<AActor>(*It))
		{
			UnrealAiViewportFrameInternal::AddActorBounds(Bounds, A);
		}
	}
	if (!Bounds.IsValid || Bounds.GetExtent().IsNearlyZero(1.f))
	{
		return UnrealAiToolJson::Error(TEXT("Empty selection or not enough geometry to frame"));
	}
	VC->FocusViewportOnBox(Bounds, true);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportCapturePng(const TSharedPtr<FJsonObject>& Args)
{
	FString RelativePath;
	if (!Args->TryGetStringField(TEXT("relative_path"), RelativePath) || RelativePath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("path"), RelativePath);
	}
	if (RelativePath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("file_path"), RelativePath);
	}
	if (RelativePath.IsEmpty())
	{
		RelativePath = TEXT("Saved/UnrealAiEditor/ViewportCaptures/viewport_capture.png");
	}
	const FString ProjectDir = FPaths::ProjectDir();
	const FString Full = FPaths::ConvertRelativePathToFull(FPaths::Combine(ProjectDir, RelativePath));
	const FString Dir = FPaths::GetPath(Full);
	IFileManager::Get().MakeDirectory(*Dir, true);
	FScreenshotRequest::RequestScreenshot(Full, false, false);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("requested_path"), Full);
	O->SetStringField(TEXT("note"), TEXT("Screenshot request queued; may complete next frame."));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportCaptureDelayed(const TSharedPtr<FJsonObject>& Args)
{
	return UnrealAiDispatch_ViewportCapturePng(Args);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ViewportSetViewMode(const TSharedPtr<FJsonObject>& Args)
{
	FEditorViewportClient* VC = GetVc();
	if (!VC)
	{
		return UnrealAiToolJson::Error(TEXT("No active editor viewport"));
	}
	FString Mode;
	if (!Args->TryGetStringField(TEXT("view_mode"), Mode) || Mode.IsEmpty())
	{
		Args->TryGetStringField(TEXT("mode"), Mode);
	}
	if (Mode.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("view_mode is required"));
	}
	const FString M = Mode.ToLower();
	EViewModeIndex Idx = VMI_Lit;
	if (M.Contains(TEXT("unlit")))
	{
		Idx = VMI_Unlit;
	}
	else if (M.Contains(TEXT("wire")) || M.Contains(TEXT("wires")))
	{
		Idx = VMI_Wireframe;
	}
	else if (M.Contains(TEXT("normal")))
	{
		Idx = VMI_VisualizeBuffer;
	}
	else if (M.Contains(TEXT("light")))
	{
		Idx = VMI_LightComplexity;
	}
	VC->SetViewMode(Idx);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("view_mode"), Mode);
	return UnrealAiToolJson::Ok(O);
}
