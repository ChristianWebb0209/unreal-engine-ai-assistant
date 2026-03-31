#include "Tools/UnrealAiToolDispatch_AssetsMaterials.h"

#include "Tools/UnrealAiToolDispatch_ArgRepair.h"
#include "Tools/UnrealAiToolJson.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Materials/Material.h"
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
	// - all_dirty: save every dirty package (matches catalog; no explicit paths required)

	bool bAllDirty = false;
	Args->TryGetBoolField(TEXT("all_dirty"), bAllDirty);
	if (!bAllDirty)
	{
		Args->TryGetBoolField(TEXT("save_all_dirty"), bAllDirty);
	}
	if (!bAllDirty)
	{
		Args->TryGetBoolField(TEXT("dirty_all"), bAllDirty);
	}

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
					UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(P);
					if (!P.IsEmpty())
					{
						InputTokens.Add(P);
					}
				}
			}
		}
		else
		{
			FString ObjOne;
			// object_path is canonical; `path` is ambiguous but tolerated as a fallback.
			if (Args->TryGetStringField(TEXT("object_path"), ObjOne) && !ObjOne.IsEmpty())
			{
				UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(ObjOne);
				if (!ObjOne.IsEmpty())
				{
					InputTokens.Add(ObjOne);
				}
			}
				else if (Args->TryGetStringField(TEXT("path"), ObjOne) && !ObjOne.IsEmpty())
			{
				UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(ObjOne);
				if (!ObjOne.IsEmpty())
				{
					InputTokens.Add(ObjOne);
				}
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
					UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(P);
					if (!P.IsEmpty())
					{
						InputTokens.Add(P);
					}
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
					UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(P);
					if (!P.IsEmpty())
					{
						InputTokens.Add(P);
					}
				}
				else if (Args->TryGetStringField(TEXT("path"), P) && !P.IsEmpty())
				{
					UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(P);
					if (!P.IsEmpty())
					{
						InputTokens.Add(P);
					}
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
		if (bAllDirty)
		{
			// Headless/editor automation: never prompt; save maps + content; fast save; no decline.
			const bool bSaved = FEditorFileUtils::SaveDirtyPackages(false, true, true, true, false, false);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("ok"), bSaved);
			O->SetBoolField(TEXT("all_dirty"), true);
			O->SetStringField(TEXT("mode"), TEXT("save_dirty_packages"));
			return UnrealAiToolJson::Ok(O);
		}
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
	Args->TryGetStringField(TEXT("from_path"), From);
	// Common model alias: object_path (same shape as from_path but key differs).
	if (From.IsEmpty())
	{
		Args->TryGetStringField(TEXT("object_path"), From);
	}
	if (From.IsEmpty())
	{
		Args->TryGetStringField(TEXT("asset_path"), From);
	}
	Args->TryGetStringField(TEXT("to_name"), ToName);
	if (ToName.IsEmpty())
	{
		Args->TryGetStringField(TEXT("new_name"), ToName);
	}
	// Alias shape often emitted by agents: { package_path, old_name, new_name }.
	if (From.IsEmpty())
	{
		FString PackagePath;
		FString OldName;
		if (Args->TryGetStringField(TEXT("package_path"), PackagePath) && !PackagePath.IsEmpty()
			&& Args->TryGetStringField(TEXT("old_name"), OldName) && !OldName.IsEmpty())
		{
			if (PackagePath.EndsWith(TEXT("/")))
			{
				PackagePath.LeftChopInline(1);
			}
			From = PackagePath + TEXT("/") + OldName + TEXT(".") + OldName;
		}
	}
	if (ToName.IsEmpty())
	{
		Args->TryGetStringField(TEXT("new_name"), ToName);
	}
	if (From.IsEmpty() || ToName.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("from_path"), TEXT("/Game/AIHarness/Task24/ScratchAsset.ScratchAsset"));
		SuggestedArgs->SetStringField(TEXT("to_name"), TEXT("ScratchAsset_Renamed"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("from_path and to_name (or new_name) are required (aliases: asset_path; or package_path+old_name+new_name)."),
			TEXT("asset_rename"),
			SuggestedArgs);
	}

	// Canonicalize inputs so downstream loads and repeated-validation signatures are stable.
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(From);
	UObject* Obj = LoadObject<UObject>(nullptr, *From);
	if (!Obj)
	{
		// Best-effort fuzzy recovery: if the model passed a stale/partial object path,
		// suggest resolving a concrete object_path first.
		FString Query;
		{
			int32 LastSlash = INDEX_NONE;
			if (From.FindLastChar(TEXT('/'), LastSlash) && LastSlash > 0 && LastSlash < From.Len() - 1)
			{
				FString Leaf = From.Mid(LastSlash + 1);
				int32 LastDot = INDEX_NONE;
				if (Leaf.FindLastChar(TEXT('.'), LastDot) && LastDot > 0 && LastDot < Leaf.Len() - 1)
				{
					Leaf = Leaf.Left(LastDot);
				}
				Leaf.TrimStartAndEndInline();
				Query = Leaf;
			}
		}
		if (Query.IsEmpty())
		{
			Query = TEXT("Asset");
		}
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("query"), Query);
		SuggestedArgs->SetStringField(TEXT("path_prefix"), TEXT("/Game"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("Could not load asset from from_path/object_path. Discover a valid /Game object path first."),
			TEXT("asset_index_fuzzy_search"),
			SuggestedArgs);
	}
	FAssetRenameData RD;
	RD.Asset = Obj;
	RD.NewName = ToName;
	RD.NewPackagePath = FPackageName::GetLongPackagePath(Obj->GetOutermost()->GetName());
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnRename", "Unreal AI: rename asset"));
	TArray<FAssetRenameData> Batch;
	Batch.Add(RD);
	const bool bRenamed = IAssetTools::Get().RenameAssets(Batch);
	if (!bRenamed)
	{
		return UnrealAiToolJson::Error(FString::Printf(TEXT("asset_rename failed (RenameAssets returned false). From='%s' ToName='%s'."), *From, *ToName));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetNumberField(TEXT("renamed_count"), 1.0);
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
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(Path);
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
		Args->TryGetStringField(TEXT("object_path"), MaterialPath);
		if (MaterialPath.IsEmpty())
		{
			Args->TryGetStringField(TEXT("path"), MaterialPath);
		}
	}
	if (MaterialPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("query"), TEXT("M_"));
		SuggestedArgs->SetStringField(TEXT("path_prefix"), TEXT("/Game"));
		SuggestedArgs->SetStringField(TEXT("class_name_substring"), TEXT("Material"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("material_path is required (aliases: object_path, path). Find a material or MI with asset_index_fuzzy_search, then pass its object path."),
			TEXT("asset_index_fuzzy_search"),
			SuggestedArgs);
	}
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(MaterialPath);
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
	if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("object_path"), MaterialPath);
		if (MaterialPath.IsEmpty())
		{
			Args->TryGetStringField(TEXT("path"), MaterialPath);
		}
	}
	if (!Args->TryGetStringField(TEXT("parameter_name"), Param) || Param.IsEmpty())
	{
		Args->TryGetStringField(TEXT("param"), Param);
	}
	if (MaterialPath.IsEmpty() || Param.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("material_path and parameter_name are required"));
	}
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(MaterialPath);
	double Val = 0.0;
	Args->TryGetNumberField(TEXT("value"), Val);
	UMaterialInstanceConstant* MI = LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);
	if (!MI)
	{
		if (LoadObject<UMaterial>(nullptr, *MaterialPath))
		{
			TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("object_path"), MaterialPath);
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("material_path resolves to a base Material, not a MaterialInstanceConstant. Scalar parameter edits require a Material Instance; duplicate the Material or create an MI, then pass the MI object path."),
				TEXT("asset_open_editor"),
				SuggestedArgs);
		}
		return UnrealAiToolJson::Error(
			TEXT("Could not load UMaterialInstanceConstant at material_path. Verify the object path (no bare .uasset file paths); use asset_index_fuzzy_search for MI assets."));
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
	if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("object_path"), MaterialPath);
		if (MaterialPath.IsEmpty())
		{
			Args->TryGetStringField(TEXT("path"), MaterialPath);
		}
	}
	if (!Args->TryGetStringField(TEXT("parameter_name"), Param) || Param.IsEmpty())
	{
		Args->TryGetStringField(TEXT("param"), Param);
	}
	if (MaterialPath.IsEmpty() || Param.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("material_path and parameter_name are required"));
	}
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(MaterialPath);
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
		if (LoadObject<UMaterial>(nullptr, *MaterialPath))
		{
			TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("object_path"), MaterialPath);
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("material_path resolves to a base Material, not a MaterialInstanceConstant. Vector parameter edits require a Material Instance; duplicate the Material or create an MI, then pass the MI object path."),
				TEXT("asset_open_editor"),
				SuggestedArgs);
		}
		return UnrealAiToolJson::Error(
			TEXT("Could not load UMaterialInstanceConstant at material_path. Verify the object path (no bare .uasset file paths); use asset_index_fuzzy_search for MI assets."));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnMatVec", "Unreal AI: MID vector"));
	MI->SetVectorParameterValueEditorOnly(FName(*Param), C);
	MI->MarkPackageDirty();
	MI->PostEditChange();
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}
