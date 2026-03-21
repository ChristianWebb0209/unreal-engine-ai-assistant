#include "Tools/UnrealAiToolDispatch_ProjectFiles.h"

#include "Tools/UnrealAiToolJson.h"
#include "Tools/UnrealAiToolProjectPathAllowlist.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_ProjectFileReadText(const TSharedPtr<FJsonObject>& Args)
{
	FString Rel;
	if (!Args->TryGetStringField(TEXT("relative_path"), Rel) || Rel.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("relative_path is required"));
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
	if (!Args->TryGetStringField(TEXT("relative_path"), Rel) || Rel.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("relative_path is required"));
	}
	Args->TryGetStringField(TEXT("content"), Content);
	Args->TryGetBoolField(TEXT("confirm"), bConfirm);
	if (!bConfirm)
	{
		return UnrealAiToolJson::Error(TEXT("confirm must be true"));
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
