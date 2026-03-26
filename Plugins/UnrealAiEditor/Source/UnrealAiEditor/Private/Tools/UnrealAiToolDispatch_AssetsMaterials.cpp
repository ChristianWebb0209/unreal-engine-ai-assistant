#include "Tools/UnrealAiToolDispatch_AssetsMaterials.h"

#include "Tools/UnrealAiToolJson.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetSavePackages(const TSharedPtr<FJsonObject>& Args)
{
	// Accept object paths (preferred) and package/folder paths (compat).
	// Examples:
	// - object_path/object_paths: `/Game/X/Asset.Asset`
	// - package_paths: `/Game/X/Asset` or `/Game/X/Folder/` (folder is expanded via Asset Registry)

	TArray<FString> InputTokens;

	// Prefer canonical object_paths if present.
	{
		const TArray<TSharedPtr<FJsonValue>>* ObjArr = nullptr;
		if (Args->TryGetArrayField(TEXT("object_paths"), ObjArr) && ObjArr && ObjArr->Num() > 0)
		{
			for (const TSharedPtr<FJsonValue>& V : *ObjArr)
			{
				FString P;
				if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty())
				{
					InputTokens.Add(P);
				}
			}
		}
		else
		{
			FString ObjOne;
			// object_path is canonical; `path` is ambiguous but tolerated as a fallback.
			if (Args->TryGetStringField(TEXT("object_path"), ObjOne) && !ObjOne.IsEmpty())
			{
				InputTokens.Add(ObjOne);
			}
				else if (Args->TryGetStringField(TEXT("path"), ObjOne) && !ObjOne.IsEmpty())
			{
				InputTokens.Add(ObjOne);
			}
		}
	}

	// Add package_paths/package_path inputs (compat + folder expansion).
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args->TryGetArrayField(TEXT("package_paths"), Arr) && Arr && Arr->Num() > 0)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FString P;
				if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty())
				{
					InputTokens.Add(P);
				}
			}
		}
		else
		{
			// legacy aliases
			if (!Args->TryGetArrayField(TEXT("packages"), Arr) || !Arr || Arr->Num() == 0)
			{
				FString P;
				if (Args->TryGetStringField(TEXT("package_path"), P) && !P.IsEmpty())
				{
					InputTokens.Add(P);
				}
				else if (Args->TryGetStringField(TEXT("path"), P) && !P.IsEmpty())
				{
					InputTokens.Add(P);
				}
			}
		}
	}

	auto MakeSuggestedArgs = [&]()
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Suggested;
		Suggested.Add(MakeShareable(new FJsonValueString(TEXT("/Game/Blueprints/MyBP.MyBP"))));
		SuggestedArgs->SetArrayField(TEXT("object_paths"), Suggested);
		return SuggestedArgs;
	};

	if (InputTokens.Num() == 0)
	{
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("No package or object paths provided to asset_save_packages."),
			TEXT("asset_save_packages"),
			MakeSuggestedArgs());
	}

	auto StripObjectPathToPackageName = [&](const FString& In)->FString
	{
		FString S = In;
		S.TrimStartAndEndInline();
		int32 Colon = INDEX_NONE;
		S.FindLastChar(TEXT(':'), Colon);
		if (Colon != INDEX_NONE)
		{
			S = S.Left(Colon);
		}
		int32 Dot = INDEX_NONE;
		S.FindLastChar(TEXT('.'), Dot);
		if (Dot != INDEX_NONE)
		{
			S = S.Left(Dot);
		}
		return S;
	};

	TSet<FString> PackageNames;

	// Expand folders via Asset Registry.
	auto ExpandFolderToPackages = [&](const FString& FolderIn)
	{
		FString Folder = FolderIn;
		Folder.TrimStartAndEndInline();
		while (Folder.EndsWith(TEXT("/")))
		{
			Folder.LeftChopInline(1);
		}
		if (Folder.IsEmpty())
		{
			return;
		}
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AR = ARM.Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(*Folder);
		Filter.bRecursivePaths = true;
		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);
		for (const FAssetData& AD : Assets)
		{
			if (AD.PackageName.ToString().IsEmpty())
			{
				continue;
			}
			PackageNames.Add(AD.PackageName.ToString());
		}
	};

	for (const FString& Token : InputTokens)
	{
		if (Token.IsEmpty())
		{
			continue;
		}
		if (Token.EndsWith(TEXT("/")))
		{
			ExpandFolderToPackages(Token);
			continue;
		}
		if (Token.Contains(TEXT(".")))
		{
			PackageNames.Add(StripObjectPathToPackageName(Token));
		}
		else
		{
			PackageNames.Add(Token);
		}
	}

	if (PackageNames.Num() == 0)
	{
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("No packages resolved for asset_save_packages input."),
			TEXT("asset_save_packages"),
			MakeSuggestedArgs());
	}

	TArray<UPackage*> Packages;
	for (const FString& PkgName : PackageNames)
	{
		if (PkgName.IsEmpty())
		{
			continue;
		}
		UPackage* Pkg = FindPackage(nullptr, *PkgName);
		if (!Pkg)
		{
			// Best-effort: if packages were dirtied/created but not loaded yet, load them.
			Pkg = LoadPackage(nullptr, *PkgName, LOAD_None);
		}
		if (Pkg)
		{
			Packages.Add(Pkg);
		}
	}

	if (Packages.Num() == 0)
	{
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("No packages resolved for asset_save_packages input."),
			TEXT("asset_save_packages"),
			MakeSuggestedArgs());
	}

	const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), bSaved);
	O->SetNumberField(TEXT("saved_count"), static_cast<double>(Packages.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetRename(const TSharedPtr<FJsonObject>& Args)
{
	FString From;
	FString ToName;
	if (!Args->TryGetStringField(TEXT("from_path"), From) || From.IsEmpty())
	{
		Args->TryGetStringField(TEXT("asset_path"), From);
	}
	Args->TryGetStringField(TEXT("to_name"), ToName);
	if (ToName.IsEmpty())
	{
		Args->TryGetStringField(TEXT("new_name"), ToName);
	}
	if (From.IsEmpty() || ToName.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("from_path and to_name (or new_name) are required"));
	}
	UObject* Obj = LoadObject<UObject>(nullptr, *From);
	if (!Obj)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load asset"));
	}
	FAssetRenameData RD;
	RD.Asset = Obj;
	RD.NewName = ToName;
	RD.NewPackagePath = FPackageName::GetLongPackagePath(Obj->GetOutermost()->GetName());
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnRename", "Unreal AI: rename asset"));
	TArray<FAssetRenameData> Batch;
	Batch.Add(RD);
	const bool bRenamed = IAssetTools::Get().RenameAssets(Batch);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), bRenamed);
	O->SetNumberField(TEXT("renamed_count"), bRenamed ? 1.0 : 0.0);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetGetMetadata(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("object_path"), Path) || Path.IsEmpty())
	{
		Args->TryGetStringField(TEXT("path"), Path);
	}
	if (Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("object_path is required"));
	}
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(Path));
	if (!AD.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("Asset not found in registry"));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("object_path"), AD.GetObjectPathString());
	O->SetStringField(TEXT("package_name"), AD.PackageName.ToString());
	O->SetStringField(TEXT("asset_name"), AD.AssetName.ToString());
	O->SetStringField(TEXT("class_path"), AD.AssetClassPath.ToString());
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_MaterialGetUsageSummary(const TSharedPtr<FJsonObject>& Args)
{
	FString MaterialPath;
	if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("material_path is required"));
	}
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();
	const FSoftObjectPath SOP(MaterialPath);
	const FAssetData AD = AR.GetAssetByObjectPath(SOP);
	if (!AD.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("Material not found in Asset Registry"));
	}
	const FAssetIdentifier Id(AD.PackageName, AD.AssetName);
	TArray<FAssetIdentifier> Referencers;
	AR.GetReferencers(Id, Referencers);
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetIdentifier& R : Referencers)
	{
		Arr.Add(MakeShareable(new FJsonValueString(R.ToString())));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetArrayField(TEXT("referencers"), Arr);
	O->SetNumberField(TEXT("count"), static_cast<double>(Arr.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_MaterialInstanceSetScalarParameter(const TSharedPtr<FJsonObject>& Args)
{
	FString MaterialPath;
	FString Param;
	if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty()
		|| !Args->TryGetStringField(TEXT("parameter_name"), Param) || Param.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("material_path and parameter_name are required"));
	}
	double Val = 0.0;
	Args->TryGetNumberField(TEXT("value"), Val);
	UMaterialInstanceConstant* MI = LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);
	if (!MI)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load UMaterialInstanceConstant"));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnMatScalar", "Unreal AI: MID scalar"));
	MI->SetScalarParameterValueEditorOnly(FName(*Param), static_cast<float>(Val));
	MI->MarkPackageDirty();
	MI->PostEditChange();
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_MaterialInstanceSetVectorParameter(const TSharedPtr<FJsonObject>& Args)
{
	FString MaterialPath;
	FString Param;
	if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty()
		|| !Args->TryGetStringField(TEXT("parameter_name"), Param) || Param.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("material_path and parameter_name are required"));
	}
	FLinearColor C(1.f, 1.f, 1.f, 1.f);
	const TSharedPtr<FJsonObject>* Vo = nullptr;
	if (Args->TryGetObjectField(TEXT("value"), Vo) && Vo->IsValid())
	{
		double R = 1.0, G = 1.0, B = 1.0, A = 1.0;
		(*Vo)->TryGetNumberField(TEXT("r"), R);
		(*Vo)->TryGetNumberField(TEXT("g"), G);
		(*Vo)->TryGetNumberField(TEXT("b"), B);
		(*Vo)->TryGetNumberField(TEXT("a"), A);
		C = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
	}
	UMaterialInstanceConstant* MI = LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);
	if (!MI)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load UMaterialInstanceConstant"));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnMatVec", "Unreal AI: MID vector"));
	MI->SetVectorParameterValueEditorOnly(FName(*Param), C);
	MI->MarkPackageDirty();
	MI->PostEditChange();
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}
