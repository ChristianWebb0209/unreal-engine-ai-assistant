#include "Backend/FUnrealAiPersistenceStub.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"

namespace UnrealAiPersistenceUtil
{
	static FString GetLocalDataRoot()
	{
#if PLATFORM_WINDOWS
		FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		if (LocalAppData.IsEmpty())
		{
			LocalAppData = FPaths::Combine(FPlatformProcess::UserDir(), TEXT("AppData/Local"));
		}
		return FPaths::Combine(LocalAppData, TEXT("UnrealAiEditor"));
#else
		return FPaths::Combine(FPlatformProcess::UserDir(), TEXT("Library/Application Support/UnrealAiEditor"));
#endif
	}
}

FUnrealAiPersistenceStub::FUnrealAiPersistenceStub() = default;

FString FUnrealAiPersistenceStub::GetDataRootDirectory() const
{
	return UnrealAiPersistenceUtil::GetLocalDataRoot();
}

FString FUnrealAiPersistenceStub::EnsureSubdir(const FString& RelativePath) const
{
	const FString Full = FPaths::Combine(GetDataRootDirectory(), RelativePath);
	IFileManager::Get().MakeDirectory(*Full, true);
	return Full;
}

bool FUnrealAiPersistenceStub::SaveSettingsJson(const FString& Json)
{
	const FString Dir = EnsureSubdir(TEXT("settings"));
	const FString Path = FPaths::Combine(Dir, TEXT("plugin_settings.json"));
	return FFileHelper::SaveStringToFile(Json, *Path);
}

bool FUnrealAiPersistenceStub::LoadSettingsJson(FString& OutJson)
{
	const FString Path = FPaths::Combine(EnsureSubdir(TEXT("settings")), TEXT("plugin_settings.json"));
	return FFileHelper::LoadFileToString(OutJson, *Path);
}

bool FUnrealAiPersistenceStub::AppendChatMessage(const FString& ProjectId, const FUnrealAiChatMessage& Message)
{
	const FString Dir = EnsureSubdir(FPaths::Combine(TEXT("chats"), ProjectId));
	const FString Path = FPaths::Combine(Dir, TEXT("messages.jsonl"));
	const FString Line = FString::Printf(TEXT("%s|%s\n"), *Message.Role, *Message.Content);
	return FFileHelper::SaveStringToFile(
		Line,
		*Path,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(),
		FILEWRITE_Append);
}

TArray<FString> FUnrealAiPersistenceStub::ListThreads(const FString& ProjectId)
{
	TArray<FString> Result;
	const FString Dir = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId);
	if (IFileManager::Get().DirectoryExists(*Dir))
	{
		IFileManager::Get().FindFiles(Result, *Dir, true, false);
	}
	return Result;
}

bool FUnrealAiPersistenceStub::SaveThreadContextJson(
	const FString& ProjectId,
	const FString& ThreadId,
	const FString& JsonBody)
{
	const FString Dir = EnsureSubdir(FPaths::Combine(TEXT("chats"), ProjectId, TEXT("threads"), ThreadId));
	const FString Path = FPaths::Combine(Dir, TEXT("context.json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadThreadContextJson(
	const FString& ProjectId,
	const FString& ThreadId,
	FString& OutJsonBody)
{
	const FString Path = FPaths::Combine(
		GetDataRootDirectory(),
		TEXT("chats"),
		ProjectId,
		TEXT("threads"),
		ThreadId,
		TEXT("context.json"));
	if (!FPaths::FileExists(Path))
	{
		return false;
	}
	return FFileHelper::LoadFileToString(OutJsonBody, *Path);
}

bool FUnrealAiPersistenceStub::SaveThreadConversationJson(
	const FString& ProjectId,
	const FString& ThreadId,
	const FString& JsonBody)
{
	const FString Dir = EnsureSubdir(FPaths::Combine(TEXT("chats"), ProjectId, TEXT("threads"), ThreadId));
	const FString Path = FPaths::Combine(Dir, TEXT("conversation.json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadThreadConversationJson(
	const FString& ProjectId,
	const FString& ThreadId,
	FString& OutJsonBody)
{
	const FString Path = FPaths::Combine(
		GetDataRootDirectory(),
		TEXT("chats"),
		ProjectId,
		TEXT("threads"),
		ThreadId,
		TEXT("conversation.json"));
	if (!FPaths::FileExists(Path))
	{
		return false;
	}
	return FFileHelper::LoadFileToString(OutJsonBody, *Path);
}
