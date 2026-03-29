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
	static void FillSuggestedArgsForReadText(TSharedPtr<FJsonObject>& OutSuggestedArgs)
	{
		OutSuggestedArgs->SetNumberField(TEXT("max_bytes"), 1024.0);
		const FString ProjectFileAbs = FPaths::GetProjectFilePath();
		if (!ProjectFileAbs.IsEmpty())
		{
			FString RelToProject = ProjectFileAbs;
			if (FPaths::MakePathRelativeTo(RelToProject, *FPaths::ProjectDir()))
			{
				RelToProject.ReplaceInline(TEXT("\\"), TEXT("/"));
				OutSuggestedArgs->SetStringField(TEXT("relative_path"), RelToProject);
				return;
			}
		}
		OutSuggestedArgs->SetStringField(TEXT("relative_path"), TEXT("Config/DefaultEngine.ini"));
	}
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ProjectFileReadText(const TSharedPtr<FJsonObject>& Args)
{
	FString Rel;
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
			UnrealAiProjectFileDispatchPriv::FillSuggestedArgsForReadText(SuggestedArgs);
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("relative_path is required (alias: file_path). For engine/plugin association, read the `.uproject` file at the project root (see suggested_correct_call); Config/DefaultEngine.ini is another common text file."),
				TEXT("project_file_read_text"),
				SuggestedArgs);
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
