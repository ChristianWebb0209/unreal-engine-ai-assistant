#include "Tools/UnrealAiToolDispatch_Environment.h"

#include "Tools/UnrealAiToolActorLookup.h"
#include "Tools/UnrealAiToolJson.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FoliageType.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "Landscape.h"
#include "LandscapeEditorUtils.h"
#include "LandscapeFileFormatInterface.h"
#include "LandscapeImportHelper.h"
#include "Misc/Paths.h"
#include "PCGComponent.h"
#include "UnrealClient.h"
#include "UnrealEdGlobals.h"

namespace UnrealAiEnvDispatchInternal
{
	static FVector RandomPointInSphere(const float Radius)
	{
		if (Radius <= KINDA_SMALL_NUMBER)
		{
			return FVector::ZeroVector;
		}
		const float U = FMath::FRand();
		const float V = FMath::FRand();
		const float Theta = 2.f * PI * U;
		const float Phi = FMath::Acos(2.f * V - 1.f);
		const float R = Radius * FMath::Pow(FMath::FRand(), 1.f / 3.f);
		return FVector(
			R * FMath::Sin(Phi) * FMath::Cos(Theta),
			R * FMath::Sin(Phi) * FMath::Sin(Theta),
			R * FMath::Cos(Phi));
	}

	static FVector GetViewportOrWorldFocus()
	{
		if (GEditor && GEditor->GetActiveViewport())
		{
			if (FEditorViewportClient* VC = static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient()))
			{
				return VC->GetViewLocation();
			}
		}
		return FVector::ZeroVector;
	}

	static AInstancedFoliageActor* FindOrSpawnInstancedFoliageActor(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			return *It;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AInstancedFoliageActor>(AInstancedFoliageActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}

	static ALandscape* FindFirstLandscape(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static bool ResolveHeightmapDiskPath(const FString& InPath, FString& OutFullPath)
	{
		if (InPath.IsEmpty())
		{
			return false;
		}
		if (FPaths::FileExists(InPath))
		{
			OutFullPath = FPaths::ConvertRelativePathToFull(InPath);
			return true;
		}
		const FString FromProject = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), InPath));
		if (FPaths::FileExists(FromProject))
		{
			OutFullPath = FromProject;
			return true;
		}
		return false;
	}
}

FUnrealAiToolInvocationResult UnrealAiDispatch_PcgGenerate(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("pcg_generate: missing arguments"));
	}
	FString ActorPath;
	if (!Args->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("pcg_generate: actor_path is required"));
	}
	UWorld* World = UnrealAiGetEditorWorld();
	if (!World)
	{
		return UnrealAiToolJson::Error(TEXT("pcg_generate: no editor world"));
	}
	AActor* Actor = UnrealAiResolveActorInWorld(World, ActorPath);
	if (!Actor)
	{
		return UnrealAiToolJson::Error(FString::Printf(TEXT("pcg_generate: actor not found: %s"), *ActorPath));
	}
	TArray<UPCGComponent*> PcgComponents;
	Actor->GetComponents<UPCGComponent>(/*out*/ PcgComponents);
	if (PcgComponents.Num() == 0)
	{
		return UnrealAiToolJson::Error(TEXT("pcg_generate: no UPCGComponent on actor"));
	}
	int32 Generated = 0;
	for (UPCGComponent* Pcg : PcgComponents)
	{
		if (!Pcg)
		{
			continue;
		}
		Pcg->Generate(true);
		Generated++;
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Payload->SetNumberField(TEXT("pcg_components_triggered"), Generated);
	return UnrealAiToolJson::Ok(Payload);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_FoliagePaintInstances(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("foliage_paint_instances: missing arguments"));
	}
	FString FoliageTypePath;
	double RadiusD = 0.0;
	if (!Args->TryGetStringField(TEXT("foliage_type"), FoliageTypePath) || FoliageTypePath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("foliage_paint_instances: foliage_type is required"));
	}
	if (!Args->TryGetNumberField(TEXT("radius"), RadiusD))
	{
		return UnrealAiToolJson::Error(TEXT("foliage_paint_instances: radius is required"));
	}
	const float Radius = static_cast<float>(RadiusD);
	if (Radius <= 0.f)
	{
		return UnrealAiToolJson::Error(TEXT("foliage_paint_instances: radius must be positive"));
	}
	FString LoadPath = FoliageTypePath;
	if (!LoadPath.StartsWith(TEXT("/")))
	{
		LoadPath = TEXT("/Game/") + LoadPath;
	}
	UFoliageType* FoliageType = LoadObject<UFoliageType>(nullptr, *LoadPath);
	if (!FoliageType)
	{
		return UnrealAiToolJson::Error(FString::Printf(TEXT("foliage_paint_instances: could not load UFoliageType: %s"), *LoadPath));
	}
	UWorld* World = UnrealAiGetEditorWorld();
	if (!World)
	{
		return UnrealAiToolJson::Error(TEXT("foliage_paint_instances: no editor world"));
	}
	AInstancedFoliageActor* IFA = UnrealAiEnvDispatchInternal::FindOrSpawnInstancedFoliageActor(World);
	if (!IFA)
	{
		return UnrealAiToolJson::Error(TEXT("foliage_paint_instances: could not find or spawn InstancedFoliageActor"));
	}
	TUniqueObj<FFoliageInfo>& InfoUnique = IFA->AddFoliageInfo(FoliageType);
	FFoliageInfo& FoliageInfo = *InfoUnique;

	const FVector Center = UnrealAiEnvDispatchInternal::GetViewportOrWorldFocus();
	const int32 InstanceCount = FMath::Clamp(static_cast<int32>(Radius / 50.f) + 1, 1, 128);
	int32 Added = 0;
	for (int32 I = 0; I < InstanceCount; ++I)
	{
		FFoliageInstance NewInst;
		NewInst.Location = Center + UnrealAiEnvDispatchInternal::RandomPointInSphere(Radius);
		NewInst.Rotation = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f);
		NewInst.PreAlignRotation = NewInst.Rotation;
		NewInst.DrawScale3D = FVector3f(1.f);
		FoliageInfo.AddInstance(FoliageType, NewInst);
		Added++;
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("foliage_type"), LoadPath);
	Payload->SetNumberField(TEXT("radius"), Radius);
	Payload->SetNumberField(TEXT("instances_added"), Added);
	Payload->SetStringField(TEXT("instanced_foliage_actor"), IFA->GetPathName());
	return UnrealAiToolJson::Ok(Payload);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_LandscapeImportHeightmap(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("landscape_import_heightmap: missing arguments"));
	}
	FString HeightmapPathIn;
	if (!Args->TryGetStringField(TEXT("heightmap_path"), HeightmapPathIn) || HeightmapPathIn.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("landscape_import_heightmap: heightmap_path is required"));
	}
	FString DiskPath;
	if (!UnrealAiEnvDispatchInternal::ResolveHeightmapDiskPath(HeightmapPathIn, DiskPath))
	{
		if (LoadObject<UTexture2D>(nullptr, *HeightmapPathIn) || LoadObject<UTexture2D>(nullptr, *(TEXT("/Game/") + HeightmapPathIn)))
		{
			return UnrealAiToolJson::Error(
				TEXT("landscape_import_heightmap: texture assets are not supported; pass a disk file path to a heightmap (.png, .r16, .raw, etc.)."));
		}
		return UnrealAiToolJson::Error(
			FString::Printf(TEXT("landscape_import_heightmap: file not found: %s"), *HeightmapPathIn));
	}
	UWorld* World = UnrealAiGetEditorWorld();
	if (!World)
	{
		return UnrealAiToolJson::Error(TEXT("landscape_import_heightmap: no editor world"));
	}
	ALandscape* Landscape = UnrealAiEnvDispatchInternal::FindFirstLandscape(World);
	if (!Landscape)
	{
		return UnrealAiToolJson::Error(
			TEXT("landscape_import_heightmap: no ALandscape found in level; create a landscape first"));
	}
	ALandscapeProxy* Proxy = Landscape;
	const FIntRect ComponentsRect = Proxy->GetBoundingRect() + Proxy->GetSectionBase();
	const int32 ReqW = ComponentsRect.Width() + 1;
	const int32 ReqH = ComponentsRect.Height() + 1;

	FLandscapeImportDescriptor Desc;
	FText Msg;
	const ELandscapeImportResult DescResult = FLandscapeImportHelper::GetHeightmapImportDescriptor(
		DiskPath,
		/*bSingleFile=*/true,
		/*bFlipYAxis=*/false,
		Desc,
		Msg);
	if (DescResult != ELandscapeImportResult::Success && DescResult != ELandscapeImportResult::Warning)
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("landscape_import_heightmap: could not read heightmap (%s): %s"),
			*DiskPath,
			*Msg.ToString()));
	}
	if (Desc.ImportResolutions.Num() == 0)
	{
		return UnrealAiToolJson::Error(TEXT("landscape_import_heightmap: import descriptor has no resolutions"));
	}
	const int32 DescriptorIndex = 0;
	TArray<uint16> RawData;
	const ELandscapeImportResult DataResult = FLandscapeImportHelper::GetHeightmapImportData(Desc, DescriptorIndex, RawData, Msg);
	if (DataResult != ELandscapeImportResult::Success && DataResult != ELandscapeImportResult::Warning)
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("landscape_import_heightmap: could not decode heightmap data: %s"),
			*Msg.ToString()));
	}
	const FLandscapeImportResolution Current = Desc.ImportResolutions[DescriptorIndex];
	FLandscapeImportResolution Required;
	Required.Width = static_cast<uint32>(ReqW);
	Required.Height = static_cast<uint32>(ReqH);

	TArray<uint16> OutData;
	if (Current.Width == Required.Width && Current.Height == Required.Height)
	{
		OutData = MoveTemp(RawData);
	}
	else
	{
		FLandscapeImportHelper::TransformHeightmapImportData(
			RawData,
			OutData,
			Current,
			Required,
			ELandscapeImportTransformType::Resample,
			FIntPoint(0, 0));
	}
	if (!LandscapeEditorUtils::SetHeightmapData(Proxy, OutData))
	{
		return UnrealAiToolJson::Error(
			FString::Printf(
				TEXT("landscape_import_heightmap: could not apply data (expected %dx%d height samples); check landscape bounds vs heightmap."),
				ReqW,
				ReqH));
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("heightmap_path"), DiskPath);
	Payload->SetStringField(TEXT("landscape_actor"), Landscape->GetPathName());
	Payload->SetNumberField(TEXT("heightmap_resolution_w"), ReqW);
	Payload->SetNumberField(TEXT("heightmap_resolution_h"), ReqH);
	return UnrealAiToolJson::Ok(Payload);
}

bool UnrealAiTryDispatchEnvironmentBuilderTool(
	const FString& ToolId,
	const TSharedPtr<FJsonObject>& Args,
	FUnrealAiToolInvocationResult& OutResult)
{
	if (ToolId == TEXT("foliage_paint_instances"))
	{
		OutResult = UnrealAiDispatch_FoliagePaintInstances(Args);
		return true;
	}
	if (ToolId == TEXT("landscape_import_heightmap"))
	{
		OutResult = UnrealAiDispatch_LandscapeImportHeightmap(Args);
		return true;
	}
	if (ToolId == TEXT("pcg_generate"))
	{
		OutResult = UnrealAiDispatch_PcgGenerate(Args);
		return true;
	}
	return false;
}
