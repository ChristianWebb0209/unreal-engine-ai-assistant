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
		return UnrealAiToolJson::Error(TEXT("object_paths array is required"));
	}
	bool bConfirm = false;
	Args->TryGetBoolField(TEXT("confirm"), bConfirm);
	if (!bConfirm)
	{
		return UnrealAiToolJson::Error(TEXT("confirm must be true to delete assets"));
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
	if (!Args->TryGetStringField(TEXT("source_path"), SourcePath) || SourcePath.IsEmpty()
		|| !Args->TryGetStringField(TEXT("dest_path"), DestPath) || DestPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("source_path and dest_path are required"));
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
		return UnrealAiToolJson::Error(TEXT("dest_path must be like /Game/Folder/AssetName"));
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
		return UnrealAiToolJson::Error(TEXT("object_path is required"));
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
