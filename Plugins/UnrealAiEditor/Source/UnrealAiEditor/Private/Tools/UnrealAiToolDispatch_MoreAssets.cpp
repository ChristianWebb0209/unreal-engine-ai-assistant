#include "Tools/UnrealAiToolDispatch_MoreAssets.h"

#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/Presentation/UnrealAiEditorNavigation.h"

#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "IAssetTools.h"
#include "ObjectTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/Object.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

static bool SplitDestPath(const FString& DestPath, FString& OutPackagePath, FString& OutAssetName)
{
	FString P = DestPath;
	if (P.EndsWith(TEXT("/")))
	{
		return false;
	}
	int32 Slash = INDEX_NONE;
	if (!P.FindLastChar(TEXT('/'), Slash))
	{
		return false;
	}
	OutPackagePath = P.Left(Slash);
	OutAssetName = P.Mid(Slash + 1);
	if (OutAssetName.IsEmpty())
	{
		return false;
	}
	return true;
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetDelete(const TSharedPtr<FJsonObject>& Args)
{
	const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
	if (!Args->TryGetArrayField(TEXT("object_paths"), Paths) || !Paths || Paths->Num() == 0)
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> SuggestedPaths;
		SuggestedPaths.Add(MakeShared<FJsonValueString>(TEXT("/Game/Blueprints/MyBP.MyBP")));
		SuggestedArgs->SetArrayField(TEXT("object_paths"), SuggestedPaths);
		SuggestedArgs->SetBoolField(TEXT("confirm"), true);
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("object_paths array is required (aliases: object_path, path)."),
			TEXT("asset_delete"),
			SuggestedArgs);
	}
	bool bConfirm = false;
	Args->TryGetBoolField(TEXT("confirm"), bConfirm);
	if (!bConfirm)
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetArrayField(TEXT("object_paths"), *Paths);
		SuggestedArgs->SetBoolField(TEXT("confirm"), true);
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("confirm must be true to delete assets"),
			TEXT("asset_delete"),
			SuggestedArgs);
	}
	TArray<UObject*> ToDelete;
	for (const TSharedPtr<FJsonValue>& V : *Paths)
	{
		FString P;
		if (!V.IsValid() || !V->TryGetString(P) || P.IsEmpty())
		{
			continue;
		}
		if (UObject* O = LoadObject<UObject>(nullptr, *P))
		{
			ToDelete.Add(O);
		}
	}
	if (ToDelete.Num() == 0)
	{
		return UnrealAiToolJson::Error(TEXT("No assets resolved for deletion"));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnAssetDel", "Unreal AI: delete assets"));
	ObjectTools::DeleteObjects(ToDelete, false);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetNumberField(TEXT("requested_count"), static_cast<double>(ToDelete.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetDuplicate(const TSharedPtr<FJsonObject>& Args)
{
	FString SourcePath;
	FString DestPath;
	if (!Args->TryGetStringField(TEXT("source_path"), SourcePath) || SourcePath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("source_object_path"), SourcePath);
	}
	if (!Args->TryGetStringField(TEXT("dest_path"), DestPath) || DestPath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("target_object_path"), DestPath);
		if (DestPath.IsEmpty())
		{
			Args->TryGetStringField(TEXT("dest_object_path"), DestPath);
		}
	}
	if (SourcePath.IsEmpty() || DestPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("source_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
		SuggestedArgs->SetStringField(TEXT("dest_path"), TEXT("/Game/Blueprints/MyBP_Copy"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("source_path and dest_path are required (aliases: source_object_path, target_object_path)."),
			TEXT("asset_duplicate"),
			SuggestedArgs);
	}

	// Accept object-path form for dest and normalize to package path (/Game/Foo/Bar.Bar -> /Game/Foo/Bar).
	{
		FString P = DestPath;
		if (P.Contains(TEXT(".")))
		{
			P = FPackageName::ObjectPathToPackageName(P) + TEXT("/") + FPackageName::GetShortName(P);
			P.ReplaceInline(TEXT("."), TEXT("/"));
		}
		// If it still looks like an object path /Game/Foo/Bar.Bar, strip the suffix.
		if (P.Contains(TEXT(".")))
		{
			P = FPackageName::ObjectPathToPackageName(P) + TEXT("/") + FPackageName::GetShortName(FPackageName::ObjectPathToPackageName(P));
		}
		DestPath = P;
	}
	UObject* Src = LoadObject<UObject>(nullptr, *SourcePath);
	if (!Src)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load source asset"));
	}
	FString DestPackagePath;
	FString DestName;
	if (!SplitDestPath(DestPath, DestPackagePath, DestName))
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("source_path"), SourcePath);
		SuggestedArgs->SetStringField(TEXT("dest_path"), TEXT("/Game/AIHarness/Task24/ScratchAsset_Copy"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("dest_path must be like /Game/Folder/AssetName (no trailing slash; not an object path)."),
			TEXT("asset_duplicate"),
			SuggestedArgs);
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnAssetDup", "Unreal AI: duplicate asset"));
	UObject* Dup = IAssetTools::Get().DuplicateAsset(DestName, DestPackagePath, Src);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), Dup != nullptr);
	if (Dup)
	{
		O->SetStringField(TEXT("new_object_path"), Dup->GetPathName());
	}
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetImport(const TSharedPtr<FJsonObject>& Args)
{
	const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
	FString DestPath;
	if (!Args->TryGetArrayField(TEXT("source_files"), Files) || !Files || Files->Num() == 0
		|| !Args->TryGetStringField(TEXT("destination_path"), DestPath) || DestPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("source_files and destination_path are required"));
	}
	TArray<UAssetImportTask*> Tasks;
	for (const TSharedPtr<FJsonValue>& V : *Files)
	{
		FString Full;
		if (!V.IsValid() || !V->TryGetString(Full) || Full.IsEmpty())
		{
			continue;
		}
		FString Abs = Full;
		if (FPaths::IsRelative(Abs))
		{
			Abs = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Abs);
		}
		if (!FPaths::FileExists(Abs))
		{
			continue;
		}
		UAssetImportTask* T = NewObject<UAssetImportTask>();
		T->Filename = Abs;
		T->DestinationPath = DestPath;
		T->bReplaceExisting = true;
		T->bAutomated = true;
		T->bSave = true;
		Tasks.Add(T);
	}
	if (Tasks.Num() == 0)
	{
		return UnrealAiToolJson::Error(TEXT("No valid source_files on disk"));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnAssetImp", "Unreal AI: import assets"));
	IAssetTools::Get().ImportAssetTasks(Tasks);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetNumberField(TEXT("task_count"), static_cast<double>(Tasks.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetOpenEditor(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("object_path"), Path) || Path.IsEmpty())
	{
		Args->TryGetStringField(TEXT("asset_path"), Path);
	}
	if (Path.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("object_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("object_path is required (alias: asset_path)."),
			TEXT("asset_open_editor"),
			SuggestedArgs);
	}
	UnrealAiNormalizeBlueprintObjectPath(Path);
	UObject* Obj = LoadObject<UObject>(nullptr, *Path);
	if (!Obj)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load asset"));
	}
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("GEditor not available"));
	}
	UnrealAiEditorNavigation::OpenAssetEditorPreferDocked(Obj);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("object_path"), Path);
	return UnrealAiToolJson::Ok(O);
}
