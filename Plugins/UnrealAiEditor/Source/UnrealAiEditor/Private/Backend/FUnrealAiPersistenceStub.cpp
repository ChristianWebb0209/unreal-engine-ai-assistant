#include "Backend/FUnrealAiPersistenceStub.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Guid.h"
#include "Harness/UnrealAiConversationJson.h"

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

	static FString SanitizeFileStem(const FString& In)
	{
		FString S = In;
		S.TrimStartAndEndInline();
		static const TCHAR* Bad = TEXT("\\/:*?\"<>|");
		for (const TCHAR* P = Bad; *P; ++P)
		{
			TCHAR Search[2] = { *P, TEXT('\0') };
			S.ReplaceInline(Search, TEXT("_"));
		}
		S.ReplaceInline(TEXT(".."), TEXT("_"));
		S.ReplaceInline(TEXT("\r"), TEXT("_"));
		S.ReplaceInline(TEXT("\n"), TEXT("_"));
		S.ReplaceInline(TEXT("\t"), TEXT("_"));
		S.TrimStartAndEndInline();
		return S;
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
			// ReplaceInline expects null-terminated TCHAR* strings; build a tiny 1-char buffer.
			TCHAR Search[2] = { *P, TEXT('\0') };
			S.ReplaceInline(Search, TEXT(""));
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
		const TSharedPtr<FJsonObject>* ThreadsObjPtr = nullptr;
		if (!Root->TryGetObjectField(TEXT("threads"), ThreadsObjPtr) || !ThreadsObjPtr || !ThreadsObjPtr->IsValid())
		{
			return true;
		}
		const TSharedPtr<FJsonObject> Threads = *ThreadsObjPtr;
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
	const FString TRoot = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"));
	if (!IFileManager::Get().DirectoryExists(*TRoot))
	{
		return Result;
	}
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(TRoot, TEXT("*-context.json")), true, false);
	for (FString FileName : Files)
	{
		if (FileName.RemoveFromEnd(TEXT("-context.json")))
		{
			Result.AddUnique(FileName);
		}
	}
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *TRoot, false, true);
	for (const FString& D : Dirs)
	{
		Result.AddUnique(D);
	}
	return Result;
}

FString FUnrealAiPersistenceStub::GetThreadStorageSlug(const FString& ProjectId, const FString& ThreadId) const
{
	return UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
}

bool FUnrealAiPersistenceStub::SaveThreadContextJson(
	const FString& ProjectId,
	const FString& ThreadId,
	const FString& JsonBody)
{
	const FString Slug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
	const FString ThreadsRoot = EnsureSubdir(FPaths::Combine(TEXT("chats"), ProjectId, TEXT("threads")));
	const FString Path = FPaths::Combine(ThreadsRoot, Slug + TEXT("-context.json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadThreadContextJson(
	const FString& ProjectId,
	const FString& ThreadId,
	FString& OutJsonBody)
{
	const FString Slug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
	const FString ThreadsRoot = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"));
	const FString Flat = FPaths::Combine(ThreadsRoot, Slug + TEXT("-context.json"));
	if (FFileHelper::LoadFileToString(OutJsonBody, *Flat))
	{
		return true;
	}
	const FString Legacy = FPaths::Combine(ThreadsRoot, Slug, TEXT("context.json"));
	if (FFileHelper::LoadFileToString(OutJsonBody, *Legacy))
	{
		return true;
	}
	return false;
}

bool FUnrealAiPersistenceStub::SaveThreadConversationJson(
	const FString& ProjectId,
	const FString& ThreadId,
	const FString& JsonBody)
{
	const FString Slug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
	const FString ThreadsRoot = EnsureSubdir(FPaths::Combine(TEXT("chats"), ProjectId, TEXT("threads")));
	const FString Path = FPaths::Combine(ThreadsRoot, Slug + TEXT("-conversation.json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadThreadConversationJson_Impl(
	const FString& ProjectId,
	const FString& ThreadId,
	FString& OutJsonBody) const
{
	const FString Slug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
	const FString ThreadsRoot = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"));
	const FString Flat = FPaths::Combine(ThreadsRoot, Slug + TEXT("-conversation.json"));
	if (FFileHelper::LoadFileToString(OutJsonBody, *Flat))
	{
		return true;
	}
	const FString Legacy = FPaths::Combine(ThreadsRoot, Slug, TEXT("conversation.json"));
	if (FFileHelper::LoadFileToString(OutJsonBody, *Legacy))
	{
		return true;
	}
	return false;
}

bool FUnrealAiPersistenceStub::LoadThreadConversationJson(
	const FString& ProjectId,
	const FString& ThreadId,
	FString& OutJsonBody)
{
	return LoadThreadConversationJson_Impl(ProjectId, ThreadId, OutJsonBody);
}

bool FUnrealAiPersistenceStub::ThreadHasUserVisibleMessages(const FString& ProjectId, const FString& ThreadId) const
{
	if (ProjectId.IsEmpty() || ThreadId.IsEmpty())
	{
		return false;
	}
	FString ConvJson;
	if (!LoadThreadConversationJson_Impl(ProjectId, ThreadId, ConvJson))
	{
		return false;
	}
	TArray<FUnrealAiConversationMessage> Messages;
	TArray<FString> Warnings;
	if (!UnrealAiConversationJson::JsonToMessages(ConvJson, Messages, Warnings))
	{
		return false;
	}
	for (const FUnrealAiConversationMessage& M : Messages)
	{
		if (M.Role.Equals(TEXT("system"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		return true;
	}
	return false;
}

bool FUnrealAiPersistenceStub::TryGetMostRecentThreadWithConversation(const FString& ProjectId, FGuid& OutThreadId) const
{
	OutThreadId = FGuid();
	if (ProjectId.IsEmpty())
	{
		return false;
	}
	const FString ThreadsRoot = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"));
	if (!IFileManager::Get().DirectoryExists(*ThreadsRoot))
	{
		return false;
	}
	TMap<FString, UnrealAiThreadIndex::FEntry> Index;
	const bool bHasIndex = UnrealAiThreadIndex::LoadIndex(this, ProjectId, Index);
	TMap<FGuid, int64> BestTicks;
	auto ConsiderPathForThread = [&BestTicks](const FString& AbsolutePath, const FGuid& ThreadGuid)
	{
		if (!ThreadGuid.IsValid() || !FPaths::FileExists(AbsolutePath))
		{
			return;
		}
		const int64 Ticks = IFileManager::Get().GetTimeStamp(*AbsolutePath).GetTicks();
		if (int64* Prev = BestTicks.Find(ThreadGuid))
		{
			if (Ticks > *Prev)
			{
				*Prev = Ticks;
			}
		}
		else
		{
			BestTicks.Add(ThreadGuid, Ticks);
		}
	};

	if (bHasIndex)
	{
		for (const TPair<FString, UnrealAiThreadIndex::FEntry>& Pair : Index)
		{
			FGuid ThreadGuid;
			if (!FGuid::Parse(Pair.Key, ThreadGuid) || !ThreadGuid.IsValid())
			{
				continue;
			}
			const FString& ResolvedSlug = Pair.Value.Slug.IsEmpty() ? Pair.Key : Pair.Value.Slug;
			ConsiderPathForThread(FPaths::Combine(ThreadsRoot, ResolvedSlug + TEXT("-conversation.json")), ThreadGuid);
			ConsiderPathForThread(FPaths::Combine(ThreadsRoot, ResolvedSlug, TEXT("conversation.json")), ThreadGuid);
		}
	}

	TArray<FString> FlatFiles;
	IFileManager::Get().FindFiles(FlatFiles, *FPaths::Combine(ThreadsRoot, TEXT("*-conversation.json")), true, false);
	for (FString FileName : FlatFiles)
	{
		if (!FileName.RemoveFromEnd(TEXT("-conversation.json")))
		{
			continue;
		}
		const FString& SlugFromFile = FileName;
		FGuid ThreadGuid;
		bool bMapped = false;
		if (bHasIndex)
		{
			for (const TPair<FString, UnrealAiThreadIndex::FEntry>& Pair : Index)
			{
				const FString& ResolvedSlug = Pair.Value.Slug.IsEmpty() ? Pair.Key : Pair.Value.Slug;
				if (ResolvedSlug != SlugFromFile)
				{
					continue;
				}
				if (FGuid::Parse(Pair.Key, ThreadGuid) && ThreadGuid.IsValid())
				{
					bMapped = true;
					break;
				}
			}
		}
		if (!bMapped)
		{
			if (!FGuid::Parse(SlugFromFile, ThreadGuid) || !ThreadGuid.IsValid())
			{
				continue;
			}
		}
		ConsiderPathForThread(FPaths::Combine(ThreadsRoot, SlugFromFile + TEXT("-conversation.json")), ThreadGuid);
	}

	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *ThreadsRoot, false, true);
	for (const FString& DirName : Dirs)
	{
		const FString LegacyConversation = FPaths::Combine(ThreadsRoot, DirName, TEXT("conversation.json"));
		if (!FPaths::FileExists(LegacyConversation))
		{
			continue;
		}
		FGuid ThreadGuid;
		bool bMapped = false;
		if (bHasIndex)
		{
			for (const TPair<FString, UnrealAiThreadIndex::FEntry>& Pair : Index)
			{
				const FString& ResolvedSlug = Pair.Value.Slug.IsEmpty() ? Pair.Key : Pair.Value.Slug;
				if (ResolvedSlug != DirName)
				{
					continue;
				}
				if (FGuid::Parse(Pair.Key, ThreadGuid) && ThreadGuid.IsValid())
				{
					bMapped = true;
					break;
				}
			}
		}
		if (!bMapped)
		{
			if (!FGuid::Parse(DirName, ThreadGuid) || !ThreadGuid.IsValid())
			{
				continue;
			}
		}
		ConsiderPathForThread(LegacyConversation, ThreadGuid);
	}

	FGuid BestGuid;
	int64 BestStampTicks = 0;
	bool bHaveCandidate = false;
	for (const TPair<FGuid, int64>& Pair : BestTicks)
	{
		const FString ThreadStr = Pair.Key.ToString(EGuidFormats::DigitsWithHyphens);
		if (!ThreadHasUserVisibleMessages(ProjectId, ThreadStr))
		{
			continue;
		}
		if (!bHaveCandidate || Pair.Value > BestStampTicks)
		{
			bHaveCandidate = true;
			BestStampTicks = Pair.Value;
			BestGuid = Pair.Key;
		}
	}
	if (!bHaveCandidate || !BestGuid.IsValid())
	{
		return false;
	}
	OutThreadId = BestGuid;
	return true;
}

void FUnrealAiPersistenceStub::ForgetThread(const FString& ProjectId, const FString& ThreadId)
{
	if (ProjectId.IsEmpty() || ThreadId.IsEmpty())
	{
		return;
	}
	const FString Slug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);
	TMap<FString, UnrealAiThreadIndex::FEntry> Index;
	if (UnrealAiThreadIndex::LoadIndex(this, ProjectId, Index))
	{
		Index.Remove(ThreadId);
		UnrealAiThreadIndex::SaveIndex(this, ProjectId, Index);
	}

	const FString ThreadsRoot = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"));
	IFileManager& FM = IFileManager::Get();
	FM.Delete(*FPaths::Combine(ThreadsRoot, Slug + TEXT("-context.json")), false, true);
	FM.Delete(*FPaths::Combine(ThreadsRoot, Slug + TEXT("-conversation.json")), false, true);
	FM.DeleteDirectory(*FPaths::Combine(ThreadsRoot, Slug), false, true);
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

	const FString OldSlug = UnrealAiThreadIndex::ResolveThreadSlug(this, ProjectId, ThreadId);

	// Ensure unique slug when another thread already owns this stem (flat file or legacy folder).
	if (OldSlug != Slug)
	{
		auto ThreadDirForSlug = [&](const FString& S) -> FString
		{
			return FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"), *S);
		};
		const FString ThreadsRoot = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"));
		const FString FlatCollide = FPaths::Combine(ThreadsRoot, Slug + TEXT("-context.json"));
		const FString NewDirInitial = ThreadDirForSlug(Slug);
		if (IFileManager::Get().FileExists(*FlatCollide) || IFileManager::Get().DirectoryExists(*NewDirInitial))
		{
			Slug = FString::Printf(TEXT("%s-%s"), *Slug, *ThreadId.Left(8));
		}
	}

	const FString ThreadsRoot = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"));
	IFileManager::Get().MakeDirectory(*ThreadsRoot, true);
	IFileManager& FM = IFileManager::Get();

	auto FlatContext = [&](const FString& S) { return FPaths::Combine(ThreadsRoot, S + TEXT("-context.json")); };
	auto FlatConversation = [&](const FString& S) { return FPaths::Combine(ThreadsRoot, S + TEXT("-conversation.json")); };
	auto LegacyContext = [&](const FString& S) { return FPaths::Combine(ThreadsRoot, S, TEXT("context.json")); };
	auto LegacyConversation = [&](const FString& S) { return FPaths::Combine(ThreadsRoot, S, TEXT("conversation.json")); };

	if (OldSlug != Slug)
	{
		auto MoveFileIfPresent = [&](const FString& From, const FString& To)
		{
			if (!FM.FileExists(*From))
			{
				return;
			}
			if (FM.FileExists(*To))
			{
				FM.Delete(*To, false, true);
			}
			FM.Move(*To, *From);
		};

		MoveFileIfPresent(FlatContext(OldSlug), FlatContext(Slug));
		MoveFileIfPresent(FlatConversation(OldSlug), FlatConversation(Slug));

		const FString OldLegDir = FPaths::Combine(ThreadsRoot, OldSlug);
		if (FM.DirectoryExists(*OldLegDir))
		{
			MoveFileIfPresent(LegacyContext(OldSlug), FlatContext(Slug));
			MoveFileIfPresent(LegacyConversation(OldSlug), FlatConversation(Slug));
			FM.DeleteDirectory(*OldLegDir, false, true);
		}
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

static const TCHAR* GOpenChatsSchema = TEXT("unreal_ai.open_chats_v1");

static FString OpenChatsPath(const FUnrealAiPersistenceStub* Persist, const FString& ProjectId)
{
	const FString ChatsDir = FPaths::Combine(Persist->GetDataRootDirectory(), TEXT("chats"), ProjectId);
	IFileManager::Get().MakeDirectory(*ChatsDir, true);
	return FPaths::Combine(ChatsDir, TEXT("ui_open_chats.json"));
}

bool FUnrealAiPersistenceStub::SaveOpenChatTabsState(
	const FString& ProjectId,
	const TArray<FGuid>& OpenThreadIdsInOrder)
{
	if (ProjectId.IsEmpty())
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), GOpenChatsSchema);
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FGuid& G : OpenThreadIdsInOrder)
	{
		if (G.IsValid())
		{
			Arr.Add(MakeShared<FJsonValueString>(G.ToString(EGuidFormats::DigitsWithHyphens)));
		}
	}
	Root->SetArrayField(TEXT("threadIds"), Arr);
	FString OutJson;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		return false;
	}
	const FString Path = OpenChatsPath(this, ProjectId);
	return FFileHelper::SaveStringToFile(OutJson, *Path);
}

bool FUnrealAiPersistenceStub::LoadOpenChatTabsState(const FString& ProjectId, TArray<FGuid>& OutOpenThreadIdsInOrder)
{
	OutOpenThreadIdsInOrder.Reset();
	if (ProjectId.IsEmpty())
	{
		return true;
	}
	const FString Path = OpenChatsPath(this, ProjectId);
	if (!FPaths::FileExists(Path))
	{
		return true;
	}
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("threadIds"), Arr) || !Arr)
	{
		return true;
	}
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		if (!V.IsValid() || V->Type != EJson::String)
		{
			continue;
		}
		const FString S = V->AsString();
		FGuid G;
		if (FGuid::Parse(S, G) && G.IsValid())
		{
			OutOpenThreadIdsInOrder.Add(G);
		}
	}
	return true;
}

bool FUnrealAiPersistenceStub::SaveProjectRecentUiJson(const FString& ProjectId, const FString& JsonBody)
{
	if (ProjectId.IsEmpty())
	{
		return false;
	}
	const FString ChatsDir = EnsureSubdir(FPaths::Combine(TEXT("chats"), ProjectId));
	const FString Path = FPaths::Combine(ChatsDir, TEXT("recent-ui.json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadProjectRecentUiJson(const FString& ProjectId, FString& OutJsonBody)
{
	OutJsonBody.Reset();
	if (ProjectId.IsEmpty())
	{
		return false;
	}
	const FString Path = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("recent-ui.json"));
	if (!FPaths::FileExists(Path))
	{
		return false;
	}
	return FFileHelper::LoadFileToString(OutJsonBody, *Path);
}

bool FUnrealAiPersistenceStub::SaveMemoryIndexJson(const FString& JsonBody)
{
	const FString Dir = EnsureSubdir(TEXT("memories"));
	const FString Path = FPaths::Combine(Dir, TEXT("index.json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadMemoryIndexJson(FString& OutJsonBody)
{
	OutJsonBody.Reset();
	const FString Path = FPaths::Combine(GetDataRootDirectory(), TEXT("memories"), TEXT("index.json"));
	if (!FPaths::FileExists(Path))
	{
		return false;
	}
	return FFileHelper::LoadFileToString(OutJsonBody, *Path);
}

bool FUnrealAiPersistenceStub::SaveMemoryTombstonesJson(const FString& JsonBody)
{
	const FString Dir = EnsureSubdir(TEXT("memories"));
	const FString Path = FPaths::Combine(Dir, TEXT("tombstones.json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadMemoryTombstonesJson(FString& OutJsonBody)
{
	OutJsonBody.Reset();
	const FString Path = FPaths::Combine(GetDataRootDirectory(), TEXT("memories"), TEXT("tombstones.json"));
	if (!FPaths::FileExists(Path))
	{
		return false;
	}
	return FFileHelper::LoadFileToString(OutJsonBody, *Path);
}

bool FUnrealAiPersistenceStub::SaveMemoryItemJson(const FString& MemoryId, const FString& JsonBody)
{
	const FString SafeId = UnrealAiPersistenceUtil::SanitizeFileStem(MemoryId);
	if (SafeId.IsEmpty())
	{
		return false;
	}
	const FString Dir = EnsureSubdir(FPaths::Combine(TEXT("memories"), TEXT("items")));
	const FString Path = FPaths::Combine(Dir, SafeId + TEXT(".json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadMemoryItemJson(const FString& MemoryId, FString& OutJsonBody)
{
	OutJsonBody.Reset();
	const FString SafeId = UnrealAiPersistenceUtil::SanitizeFileStem(MemoryId);
	if (SafeId.IsEmpty())
	{
		return false;
	}
	const FString Path = FPaths::Combine(GetDataRootDirectory(), TEXT("memories"), TEXT("items"), SafeId + TEXT(".json"));
	if (!FPaths::FileExists(Path))
	{
		return false;
	}
	return FFileHelper::LoadFileToString(OutJsonBody, *Path);
}

bool FUnrealAiPersistenceStub::DeleteMemoryItemJson(const FString& MemoryId)
{
	const FString SafeId = UnrealAiPersistenceUtil::SanitizeFileStem(MemoryId);
	if (SafeId.IsEmpty())
	{
		return false;
	}
	const FString Path = FPaths::Combine(GetDataRootDirectory(), TEXT("memories"), TEXT("items"), SafeId + TEXT(".json"));
	if (!FPaths::FileExists(Path))
	{
		return true;
	}
	return IFileManager::Get().Delete(*Path, false, true);
}

void FUnrealAiPersistenceStub::ListMemoryItemIds(TArray<FString>& OutMemoryIds) const
{
	OutMemoryIds.Reset();
	const FString ItemsDir = FPaths::Combine(GetDataRootDirectory(), TEXT("memories"), TEXT("items"));
	if (!IFileManager::Get().DirectoryExists(*ItemsDir))
	{
		return;
	}
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(ItemsDir, TEXT("*.json")), true, false);
	for (FString& FileName : Files)
	{
		if (FileName.RemoveFromEnd(TEXT(".json")))
		{
			OutMemoryIds.Add(FileName);
		}
	}
}

bool FUnrealAiPersistenceStub::SaveMemoryGenerationStatusJson(const FString& JsonBody)
{
	const FString Dir = EnsureSubdir(TEXT("memories"));
	const FString Path = FPaths::Combine(Dir, TEXT("generation_status.json"));
	return FFileHelper::SaveStringToFile(JsonBody, *Path);
}

bool FUnrealAiPersistenceStub::LoadMemoryGenerationStatusJson(FString& OutJsonBody)
{
	OutJsonBody.Reset();
	const FString Path = FPaths::Combine(GetDataRootDirectory(), TEXT("memories"), TEXT("generation_status.json"));
	if (!FPaths::FileExists(Path))
	{
		return false;
	}
	return FFileHelper::LoadFileToString(OutJsonBody, *Path);
}

void FUnrealAiPersistenceStub::ListPersistedThreadsForHistory(
	const FString& ProjectId,
	TArray<FString>& OutThreadIds,
	TArray<FString>& OutDisplayNames) const
{
	OutThreadIds.Reset();
	OutDisplayNames.Reset();
	if (ProjectId.IsEmpty())
	{
		return;
	}
	TMap<FString, UnrealAiThreadIndex::FEntry> Index;
	if (!UnrealAiThreadIndex::LoadIndex(this, ProjectId, Index))
	{
		return;
	}
	struct FRow
	{
		FString Id;
		FString Label;
	};
	TArray<FRow> Rows;
	Rows.Reserve(Index.Num());
	for (const TPair<FString, UnrealAiThreadIndex::FEntry>& Pair : Index)
	{
		FRow R;
		R.Id = Pair.Key;
		R.Label = Pair.Value.DisplayName.IsEmpty() ? Pair.Key : Pair.Value.DisplayName;
		Rows.Add(MoveTemp(R));
	}
	Rows.Sort([](const FRow& A, const FRow& B) { return A.Label.Compare(B.Label, ESearchCase::IgnoreCase) < 0; });
	for (const FRow& R : Rows)
	{
		OutThreadIds.Add(R.Id);
		OutDisplayNames.Add(R.Label);
	}
}

bool FUnrealAiPersistenceStub::DeleteAllLocalChatData(FString& OutError)
{
	OutError.Reset();
	IFileManager& FM = IFileManager::Get();
	const FString ChatsDir = FPaths::Combine(GetDataRootDirectory(), TEXT("chats"));
	if (FPaths::DirectoryExists(ChatsDir))
	{
		if (!FM.DeleteDirectory(*ChatsDir, false, true))
		{
			OutError = TEXT("Failed to delete the local chats directory.");
			return false;
		}
	}
	const FString ProjSavedPlugin = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor"));
	if (FPaths::DirectoryExists(ProjSavedPlugin))
	{
		if (!FM.DeleteDirectory(*ProjSavedPlugin, false, true))
		{
			OutError = TEXT("Failed to delete Saved/UnrealAiEditor.");
			return false;
		}
	}
	return true;
}
