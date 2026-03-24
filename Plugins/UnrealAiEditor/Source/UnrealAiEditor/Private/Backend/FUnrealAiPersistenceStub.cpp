#include "Backend/FUnrealAiPersistenceStub.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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

namespace UnrealAiThreadIndex
{
	static const TCHAR* Schema = TEXT("unreal_ai.thread_index_v1");

	struct FEntry
	{
		FString Slug;
		FString DisplayName;
	};

	static FString IndexPath(
		const FUnrealAiPersistenceStub* Persist,
		const FString& ProjectId)
	{
		// chats/<ProjectId>/threads_index.json
		const FString DataRoot = Persist->GetDataRootDirectory();
		const FString ChatsDir = FPaths::Combine(DataRoot, TEXT("chats"), ProjectId);
		IFileManager::Get().MakeDirectory(*ChatsDir, true);
		return FPaths::Combine(ChatsDir, TEXT("threads_index.json"));
	}

	static FString SanitizeSlug(const FString& InName)
	{
		FString S = InName;
		S.TrimStartAndEndInline();
		S.ReplaceInline(TEXT("\""), TEXT(""));
		S.ReplaceInline(TEXT("'"), TEXT(""));

		// Replace whitespace with dashes.
		S.ReplaceInline(TEXT("\t"), TEXT("-"));
		S.ReplaceInline(TEXT("\n"), TEXT("-"));
		S.ReplaceInline(TEXT("\r"), TEXT("-"));
		S.ReplaceInline(TEXT(" "), TEXT("-"));

		// Collapse repeated dashes.
		while (S.Contains(TEXT("--")))
		{
			S = S.Replace(TEXT("--"), TEXT("-"));
		}

		// Remove characters that are commonly invalid in filenames on Windows.
		static const TCHAR* Bad = TEXT("\\/:*?\"<>|");
		for (const TCHAR* P = Bad; *P; ++P)
		{
			S.ReplaceInline(FString::Chr(*P), TEXT(""));
		}

		// Final trim.
		S.TrimStartAndEndInline();
		if (S.IsEmpty())
		{
			return FString();
		}
		// Avoid overly long paths.
		const int32 MaxLen = 64;
		if (S.Len() > MaxLen)
		{
			S = S.Left(MaxLen);
			S.TrimEndInline();
		}
		return S;
	}

	static bool LoadIndex(const FUnrealAiPersistenceStub* Persist, const FString& ProjectId, TMap<FString, FEntry>& Out)
	{
		Out.Reset();
		if (!Persist)
		{
			return false;
		}
		const FString Path = IndexPath(Persist, ProjectId);
		if (!FPaths::FileExists(Path))
		{
			return true; // empty index
		}
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
		{
			return false;
		}
		FString FoundSchema;
		Root->TryGetStringField(TEXT("schema"), FoundSchema);
		if (!FoundSchema.IsEmpty() && FoundSchema != Schema)
		{
			return false;
		}
		TSharedPtr<FJsonObject> ThreadsObj;
		if (!Root->TryGetObjectField(TEXT("threads"), ThreadsObj) || !ThreadsObj.IsValid())
		{
			return true;
		}
		const TSharedPtr<FJsonObject> Threads = ThreadsObj;
		for (const auto& Pair : Threads->Values)
		{
			const FString ThreadId = Pair.Key;
			const TSharedPtr<FJsonValue>& V = Pair.Value;
			const TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
			if (!Obj.IsValid())
			{
				continue;
			}
			FEntry E;
			Obj->TryGetStringField(TEXT("slug"), E.Slug);
			Obj->TryGetStringField(TEXT("display_name"), E.DisplayName);
			if (!E.Slug.IsEmpty() || !E.DisplayName.IsEmpty())
			{
				Out.Add(ThreadId, MoveTemp(E));
			}
		}
		return true;
	}

	static bool SaveIndex(const FUnrealAiPersistenceStub* Persist, const FString& ProjectId, const TMap<FString, FEntry>& In)
	{
		if (!Persist)
		{
			return false;
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), Schema);

		TSharedPtr<FJsonObject> Threads = MakeShared<FJsonObject>();
		for (const auto& Pair : In)
		{
			const FString& ThreadId = Pair.Key;
			const FEntry& E = Pair.Value;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("slug"), E.Slug);
			Obj->SetStringField(TEXT("display_name"), E.DisplayName);
			Threads->SetObjectField(ThreadId, Obj);
		}
		Root->SetObjectField(TEXT("threads"), Threads);

		FString OutJson;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), W))
		{
			return false;
		}
		const FString Path = IndexPath(Persist, ProjectId);
		return FFileHelper::SaveStringToFile(OutJson, *Path);
	}

	static FString ResolveThreadSlug(const FUnrealAiPersistenceStub* Persist, const FString& ProjectId, const FString& ThreadId)
	{
		TMap<FString, FEntry> Index;
		if (!LoadIndex(Persist, ProjectId, Index))
		{
			return ThreadId;
		}
		if (const FEntry* E = Index.Find(ThreadId))
		{
			if (!E->Slug.IsEmpty())
			{
				return E->Slug;
			}
		}
		return ThreadId;
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

bool FUnrealAiPersistenceStub::SaveUsageStatsJson(const FString& Json)
{
	const FString Dir = EnsureSubdir(TEXT("settings"));
	const FString Path = FPaths::Combine(Dir, TEXT("usage_stats.json"));
	return FFileHelper::SaveStringToFile(Json, *Path);
}

bool FUnrealAiPersistenceStub::LoadUsageStatsJson(FString& OutJson)
{
	const FString Path = FPaths::Combine(EnsureSubdir(TEXT("settings")), TEXT("usage_stats.json"));
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
	const FString Slug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
	const FString Dir = EnsureSubdir(FPaths::Combine(TEXT("chats"), ProjectId, TEXT("threads"), Slug));
	const FString Path = FPaths::Combine(Dir, TEXT("context.json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadThreadContextJson(
	const FString& ProjectId,
	const FString& ThreadId,
	FString& OutJsonBody)
{
	const FString Slug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
	const FString Path = FPaths::Combine(
		GetDataRootDirectory(),
		TEXT("chats"),
		ProjectId,
		TEXT("threads"),
		Slug,
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
	const FString Slug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
	const FString Dir = EnsureSubdir(FPaths::Combine(TEXT("chats"), ProjectId, TEXT("threads"), Slug));
	const FString Path = FPaths::Combine(Dir, TEXT("conversation.json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadThreadConversationJson(
	const FString& ProjectId,
	const FString& ThreadId,
	FString& OutJsonBody)
{
	const FString Slug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
	const FString Path = FPaths::Combine(
		GetDataRootDirectory(),
		TEXT("chats"),
		ProjectId,
		TEXT("threads"),
		Slug,
		TEXT("conversation.json"));
	if (!FPaths::FileExists(Path))
	{
		return false;
	}
	return FFileHelper::LoadFileToString(OutJsonBody, *Path);
}

bool FUnrealAiPersistenceStub::SetThreadChatName(
	const FString& ProjectId,
	const FString& ThreadId,
	const FString& ChatName)
{
	if (ProjectId.IsEmpty() || ThreadId.IsEmpty())
	{
		return false;
	}

	FString DisplayName = ChatName;
	DisplayName.TrimStartAndEndInline();
	if (DisplayName.IsEmpty())
	{
		return false;
	}

	const FString SlugBase = UnrealAiThreadIndex::SanitizeSlug(DisplayName);
	FString Slug = SlugBase;
	if (Slug.IsEmpty())
	{
		return false;
	}

	TMap<FString, UnrealAiThreadIndex::FEntry> Index;
	{
		// We need a mutable index map for the helpers.
		if (!UnrealAiThreadIndex::LoadIndex(this, ProjectId, Index))
		{
			Index.Reset();
		}
	}

	// Ensure unique slug if there's already a directory by that name.
	auto ThreadDirForSlug = [&](const FString& S) -> FString
	{
		return FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"), *S);
	};

	const FString OldSlug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
	FString OldDir = ThreadDirForSlug(OldSlug);
	const FString NewDirInitial = ThreadDirForSlug(Slug);
	if (OldSlug != Slug && IFileManager::Get().DirectoryExists(*NewDirInitial))
	{
		// Collision: append a stable suffix based on thread id.
		Slug = FString::Printf(TEXT("%s-%s"), *Slug, *ThreadId.Left(8));
	}

	const FString NewDir = ThreadDirForSlug(Slug);
	const FString ThreadsRoot = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"));
	IFileManager::Get().MakeDirectory(*ThreadsRoot, true);

	const bool bOldExists = IFileManager::Get().DirectoryExists(*OldDir);
	const bool bNewExists = IFileManager::Get().DirectoryExists(*NewDir);

	if (OldSlug != Slug && bOldExists && !bNewExists)
	{
		// If it somehow didn't exist (race), move the directory.
		IFileManager::Get().Move(*NewDir, *OldDir);
	}
	else if (OldSlug != Slug && bOldExists && bNewExists)
	{
		// Target exists; try move anyway (unique slug should usually prevent this).
		IFileManager::Get().Move(*NewDir, *OldDir);
	}
	else if (!bOldExists && !bNewExists)
	{
		// New chat with no persisted thread folder yet.
		IFileManager::Get().MakeDirectory(*NewDir, true);
	}

	Index.FindOrAdd(ThreadId) = UnrealAiThreadIndex::FEntry{Slug, DisplayName};
	return UnrealAiThreadIndex::SaveIndex(this, ProjectId, Index);
}

bool FUnrealAiPersistenceStub::GetThreadChatName(
	const FString& ProjectId,
	const FString& ThreadId,
	FString& OutChatName)
{
	OutChatName.Reset();
	if (ProjectId.IsEmpty() || ThreadId.IsEmpty())
	{
		return false;
	}

	TMap<FString, UnrealAiThreadIndex::FEntry> Index;
	if (!UnrealAiThreadIndex::LoadIndex(this, ProjectId, Index))
	{
		return false;
	}
	if (const UnrealAiThreadIndex::FEntry* E = Index.Find(ThreadId))
	{
		OutChatName = E->DisplayName;
		return !OutChatName.IsEmpty();
	}
	return true; // not found = empty
}
