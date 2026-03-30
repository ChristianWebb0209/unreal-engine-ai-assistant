#include "Tools/UnrealAiToolDispatch_ProjectFiles.h"

#include "Tools/UnrealAiToolJson.h"
#include "Tools/UnrealAiToolProjectPathAllowlist.h"
#include "Tools/UnrealAiToolDispatch_ArgRepair.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UnrealAiProjectFileDispatchPriv
{
	/** Prefer the real `.uproject` path (project-relative); fallback to a config file if unavailable. */
	static FString DefaultRelativePathForReadText()
	{
		const FString ProjectFileAbs = FPaths::GetProjectFilePath();
		if (!ProjectFileAbs.IsEmpty())
		{
			FString RelToProject = ProjectFileAbs;
			if (FPaths::MakePathRelativeTo(RelToProject, *FPaths::ProjectDir()))
			{
				RelToProject.ReplaceInline(TEXT("\\"), TEXT("/"));
				return RelToProject;
			}
		}
		return TEXT("Config/DefaultEngine.ini");
	}

	static void FillSuggestedArgsForReadText(TSharedPtr<FJsonObject>& OutSuggestedArgs)
	{
		OutSuggestedArgs->SetNumberField(TEXT("max_bytes"), 1024.0);
		OutSuggestedArgs->SetStringField(TEXT("relative_path"), DefaultRelativePathForReadText());
	}
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ProjectFileReadText(const TSharedPtr<FJsonObject>& Args)
{
	FString Rel;
	bool bRelativePathDefaulted = false;
	{
		const TArray<const TCHAR*> Aliases = { TEXT("file_path") };
		if (!UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
			Args,
			TEXT("relative_path"),
			Aliases,
			Rel)
			|| Rel.IsEmpty())
		{
			Rel = UnrealAiProjectFileDispatchPriv::DefaultRelativePathForReadText();
			bRelativePathDefaulted = true;
		}
	}
	int32 MaxBytes = 512 * 1024;
	double MB = 0.0;
	if (Args->TryGetNumberField(TEXT("max_bytes"), MB))
	{
		MaxBytes = FMath::Clamp(static_cast<int32>(MB), 1024, 4 * 1024 * 1024);
	}
	FString Abs;
	FString Err;
	if (!UnrealAiResolveProjectFilePath(Rel, Abs, Err))
	{
		return UnrealAiToolJson::Error(Err);
	}
	if (!FPaths::FileExists(Abs))
	{
		return UnrealAiToolJson::Error(TEXT("File does not exist"));
	}
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *Abs))
	{
		return UnrealAiToolJson::Error(TEXT("Failed to read file"));
	}
	const bool bTruncated = Content.Len() > MaxBytes;
	if (bTruncated)
	{
		Content.LeftInline(MaxBytes);
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("relative_path"), Rel);
	if (bRelativePathDefaulted)
	{
		O->SetBoolField(TEXT("relative_path_defaulted"), true);
	}
	O->SetStringField(TEXT("content"), Content);
	O->SetBoolField(TEXT("truncated"), bTruncated);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ProjectFileWriteText(const TSharedPtr<FJsonObject>& Args)
{
	FString Rel;
	FString Content;
	bool bConfirm = false;
	{
		const TArray<const TCHAR*> Aliases = { TEXT("file_path") };
		if (!UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
			Args,
			TEXT("relative_path"),
			Aliases,
			Rel)
			|| Rel.IsEmpty())
		{
			TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("relative_path"), TEXT("Config/DefaultEngine.ini"));
			SuggestedArgs->SetStringField(TEXT("content"), TEXT("// Written by UnrealAiEditor"));
			SuggestedArgs->SetBoolField(TEXT("confirm"), true);
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("relative_path is required (alias: file_path)."),
				TEXT("project_file_write_text"),
				SuggestedArgs);
		}
	}
	Args->TryGetStringField(TEXT("content"), Content);
	Args->TryGetBoolField(TEXT("confirm"), bConfirm);
	if (!bConfirm)
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("relative_path"), Rel);
		SuggestedArgs->SetStringField(TEXT("content"), Content);
		SuggestedArgs->SetBoolField(TEXT("confirm"), true);
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("confirm must be true for project_file_write_text."),
			TEXT("project_file_write_text"),
			SuggestedArgs);
	}
	FString Abs;
	FString Err;
	if (!UnrealAiResolveProjectFilePath(Rel, Abs, Err))
	{
		return UnrealAiToolJson::Error(Err);
	}
	const FString Dir = FPaths::GetPath(Abs);
	IFileManager::Get().MakeDirectory(*Dir, true);
	if (!FFileHelper::SaveStringToFile(Content, *Abs))
	{
		return UnrealAiToolJson::Error(TEXT("Failed to write file"));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("relative_path"), Rel);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ProjectFileMove(const TSharedPtr<FJsonObject>& Args)
{
	FString FromRel;
	FString ToRel;
	bool bConfirm = false;
	bool bReplace = false;
	{
		const TArray<const TCHAR*> FromAliases = { TEXT("from_path"), TEXT("source_relative_path") };
		if (!UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
				Args,
				TEXT("from_relative_path"),
				FromAliases,
				FromRel)
			|| FromRel.IsEmpty())
		{
			TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("from_relative_path"), TEXT("Source/MyProject/Old.cpp"));
			SuggestedArgs->SetStringField(TEXT("to_relative_path"), TEXT("Source/MyProject/New.cpp"));
			SuggestedArgs->SetBoolField(TEXT("confirm"), true);
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("from_relative_path is required (aliases: from_path, source_relative_path)."),
				TEXT("project_file_move"),
				SuggestedArgs);
		}
	}
	{
		const TArray<const TCHAR*> ToAliases = { TEXT("to_path"), TEXT("dest_relative_path") };
		if (!UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
				Args,
				TEXT("to_relative_path"),
				ToAliases,
				ToRel)
			|| ToRel.IsEmpty())
		{
			TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("from_relative_path"), FromRel);
			SuggestedArgs->SetStringField(TEXT("to_relative_path"), TEXT("Source/MyProject/New.cpp"));
			SuggestedArgs->SetBoolField(TEXT("confirm"), true);
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("to_relative_path is required (aliases: to_path, dest_relative_path)."),
				TEXT("project_file_move"),
				SuggestedArgs);
		}
	}
	Args->TryGetBoolField(TEXT("confirm"), bConfirm);
	if (!bConfirm)
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("from_relative_path"), FromRel);
		SuggestedArgs->SetStringField(TEXT("to_relative_path"), ToRel);
		SuggestedArgs->SetBoolField(TEXT("replace_existing"), false);
		SuggestedArgs->SetBoolField(TEXT("confirm"), true);
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("confirm must be true for project_file_move."),
			TEXT("project_file_move"),
			SuggestedArgs);
	}
	Args->TryGetBoolField(TEXT("replace_existing"), bReplace);

	FString FromAbs;
	FString ToAbs;
	FString Err;
	if (!UnrealAiResolveProjectFilePath(FromRel, FromAbs, Err))
	{
		return UnrealAiToolJson::Error(Err);
	}
	if (!UnrealAiResolveProjectFilePath(ToRel, ToAbs, Err))
	{
		return UnrealAiToolJson::Error(Err);
	}
	if (FromAbs.Equals(ToAbs, ESearchCase::IgnoreCase))
	{
		return UnrealAiToolJson::Error(TEXT("from_relative_path and to_relative_path resolve to the same file"));
	}
	if (!FPaths::FileExists(FromAbs))
	{
		return UnrealAiToolJson::Error(TEXT("Source file does not exist"));
	}
	if (FPaths::FileExists(ToAbs) && !bReplace)
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("from_relative_path"), FromRel);
		SuggestedArgs->SetStringField(TEXT("to_relative_path"), ToRel);
		SuggestedArgs->SetBoolField(TEXT("replace_existing"), true);
		SuggestedArgs->SetBoolField(TEXT("confirm"), true);
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("Destination file already exists. Pass replace_existing:true to overwrite."),
			TEXT("project_file_move"),
			SuggestedArgs);
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ToAbs), true);
	const bool bMoved = IFileManager::Get().Move(*ToAbs, *FromAbs, true, true, true, true);
	if (!bMoved)
	{
		return UnrealAiToolJson::Error(
			TEXT("Move failed (file may be locked, read-only, or on a volume that does not support atomic move)."));
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("from_relative_path"), FromRel);
	O->SetStringField(TEXT("to_relative_path"), ToRel);
	O->SetBoolField(TEXT("replace_existing"), bReplace);
	return UnrealAiToolJson::Ok(O);
}
