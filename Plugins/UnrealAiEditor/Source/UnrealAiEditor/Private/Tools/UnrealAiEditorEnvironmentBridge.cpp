#include "Tools/UnrealAiEditorEnvironmentBridge.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAiEnv, Log, All);

namespace UnrealAiEditorEnvironmentBridge
{
	static void AppendMeshEntry(
		const FAssetData& Asset,
		TArray<TSharedPtr<FJsonValue>>& MeshArray,
		int32& RegistryHitCount,
		int32& HardLoadCount)
	{
		TSharedRef<FJsonObject> MeshObj = MakeShared<FJsonObject>();
		MeshObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		MeshObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		MeshObj->SetStringField(TEXT("package"), Asset.PackageName.ToString());

		FString PackagePath = Asset.PackagePath.ToString();
		PackagePath.RemoveFromStart(TEXT("/Game/"));
		int32 SlashIndex;
		if (PackagePath.FindChar('/', SlashIndex))
		{
			PackagePath = PackagePath.Left(SlashIndex);
		}
		MeshObj->SetStringField(TEXT("category"), PackagePath);

		float SizeX = 0, SizeY = 0, SizeZ = 0;
		bool bFoundInRegistry = false;

		FString ApproxSizeStr;
		if (Asset.GetTagValue(FName("ApproxSize"), ApproxSizeStr))
		{
			TArray<FString> Dims;
			if (ApproxSizeStr.ParseIntoArray(Dims, TEXT("x"), true) == 3)
			{
				SizeX = FCString::Atof(*Dims[0]);
				SizeY = FCString::Atof(*Dims[1]);
				SizeZ = FCString::Atof(*Dims[2]);
				bFoundInRegistry = true;
				RegistryHitCount++;
			}
		}
		if (!bFoundInRegistry)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
			if (!Mesh)
			{
				Mesh = Cast<UStaticMesh>(Asset.ToSoftObjectPath().TryLoad());
			}
			if (Mesh)
			{
				const FBoxSphereBounds Bounds = Mesh->GetBounds();
				SizeX = Bounds.BoxExtent.X * 2.0f;
				SizeY = Bounds.BoxExtent.Y * 2.0f;
				SizeZ = Bounds.BoxExtent.Z * 2.0f;
				HardLoadCount++;
			}
		}

		MeshObj->SetNumberField(TEXT("sizeX"), SizeX);
		MeshObj->SetNumberField(TEXT("sizeY"), SizeY);
		MeshObj->SetNumberField(TEXT("sizeZ"), SizeZ);

		MeshArray.Add(MakeShared<FJsonValueObject>(MeshObj.ToSharedPtr()));
	}

	FString ExportStaticMeshesJson()
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		AssetRegistry.ScanPathsSynchronous({TEXT("/Game")}, /*bForceRescan=*/true);

		FARFilter Filter;
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> AssetList;
		AssetRegistry.GetAssets(Filter, AssetList);

		int32 RegistryHitCount = 0;
		int32 HardLoadCount = 0;

		TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> MeshArray;

		for (const FAssetData& Asset : AssetList)
		{
			AppendMeshEntry(Asset, MeshArray, RegistryHitCount, HardLoadCount);
		}

		RootObject->SetArrayField(TEXT("meshes"), MeshArray);
		RootObject->SetNumberField(TEXT("count"), MeshArray.Num());

		FString OutputString;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(RootObject, Writer);

		UE_LOG(LogUnrealAiEnv, Log, TEXT("Static meshes: total=%d registry=%d loaded=%d"), MeshArray.Num(), RegistryHitCount, HardLoadCount);

		return OutputString;
	}

	FString ExportStaticMeshesJsonForPath(const FString& FolderPath)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FString SearchPath = FolderPath;
		if (!SearchPath.StartsWith(TEXT("/Game")))
		{
			SearchPath = TEXT("/Game/") + SearchPath;
		}

		AssetRegistry.ScanPathsSynchronous({SearchPath}, /*bForceRescan=*/true);

		FARFilter Filter;
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(*SearchPath));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> AssetList;
		AssetRegistry.GetAssets(Filter, AssetList);

		int32 RegistryHitCount = 0;
		int32 HardLoadCount = 0;

		TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> MeshArray;

		for (const FAssetData& Asset : AssetList)
		{
			AppendMeshEntry(Asset, MeshArray, RegistryHitCount, HardLoadCount);
		}

		RootObject->SetArrayField(TEXT("meshes"), MeshArray);
		RootObject->SetNumberField(TEXT("count"), MeshArray.Num());
		RootObject->SetStringField(TEXT("searchPath"), SearchPath);

		FString OutputString;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(RootObject, Writer);

		UE_LOG(LogUnrealAiEnv, Log, TEXT("Static meshes by path: total=%d in %s"), MeshArray.Num(), *SearchPath);

		return OutputString;
	}
}
