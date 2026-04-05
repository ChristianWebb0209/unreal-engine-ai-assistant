#include "Retrieval/UnrealAiVectorIndexStore.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"

namespace
{
	static FCriticalSection GVectorDbOpenMutex;

	static FString GetVectorProjectRootForStore(const FString& ProjectId)
	{
		const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		const FString Base = LocalAppData.IsEmpty()
			? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor"))
			: FPaths::Combine(LocalAppData, TEXT("UnrealAiEditor"));
		return FPaths::Combine(Base, TEXT("vector"), ProjectId);
	}

	static float CosineSimilarity(const TArray<float>& A, const TArray<float>& B)
	{
		if (A.Num() == 0 || A.Num() != B.Num())
		{
			return 0.0f;
		}
		double Dot = 0.0;
		double NormA = 0.0;
		double NormB = 0.0;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			Dot += static_cast<double>(A[i]) * static_cast<double>(B[i]);
			NormA += static_cast<double>(A[i]) * static_cast<double>(A[i]);
			NormB += static_cast<double>(B[i]) * static_cast<double>(B[i]);
		}
		if (NormA <= KINDA_SMALL_NUMBER || NormB <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		return static_cast<float>(Dot / (FMath::Sqrt(NormA) * FMath::Sqrt(NormB)));
	}
}

FUnrealAiVectorIndexStore::FUnrealAiVectorIndexStore(const FString& InProjectId)
	: ProjectId(InProjectId)
	, RootDir(GetVectorProjectRootForStore(InProjectId))
{
}

FUnrealAiVectorIndexStore::~FUnrealAiVectorIndexStore()
{
	CloseDb();
}

bool FUnrealAiVectorIndexStore::Initialize(FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	return EnsureInitializedUnlocked(OutError);
}

bool FUnrealAiVectorIndexStore::EnsureInitializedUnlocked(FString& OutError)
{
	IFileManager::Get().MakeDirectory(*RootDir, true);
	return OpenDb(OutError) && EnsureSchema(OutError);
}

bool FUnrealAiVectorIndexStore::OpenDb(FString& OutError)
{
	FScopeLock OpenLock(&GVectorDbOpenMutex);
	if (Db != nullptr)
	{
		return true;
	}
	const FString DbPath = GetIndexDbPath();
	constexpr int32 MaxAttempts = 6;
	for (int32 Attempt = 1; Attempt <= MaxAttempts; ++Attempt)
	{
		Db = new FSQLiteDatabase();
		if (Db->Open(*DbPath, ESQLiteDatabaseOpenMode::ReadWriteCreate))
		{
			// Safe defaults for mixed read/write access across harness + background indexer.
			Db->Execute(TEXT("PRAGMA busy_timeout=8000;"));
			Db->Execute(TEXT("PRAGMA journal_mode=WAL;"));
			Db->Execute(TEXT("PRAGMA synchronous=NORMAL;"));
			return true;
		}

		CloseDbUnlocked();
		if (Attempt < MaxAttempts)
		{
			const float BackoffSec = FMath::Min(0.5f, 0.05f * FMath::Pow(2.f, static_cast<float>(Attempt)));
			FPlatformProcess::Sleep(BackoffSec);
		}
	}
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("UnrealAi vector SQLite: failed to open DB after %d attempts (path=%s). See manifest vector_db_open_retry_not_before_utc for cooldown."),
		MaxAttempts,
		*DbPath);
	OutError = FString::Printf(TEXT("Failed to open SQLite DB at %s after retries."), *DbPath);
	return false;
}

void FUnrealAiVectorIndexStore::CloseDb()
{
	FScopeLock Lock(&DbMutex);
	CloseDbUnlocked();
}

void FUnrealAiVectorIndexStore::CloseDbUnlocked()
{
	if (!Db)
	{
		return;
	}
	FScopeLock OpenLock(&GVectorDbOpenMutex);
	// Drop any unexpected transaction so sqlite3_close is more likely to succeed.
	Db->Execute(TEXT("ROLLBACK;"));
	constexpr int32 MaxAttempts = 80;
	for (int32 Attempt = 0; Attempt < MaxAttempts && Db && Db->IsValid(); ++Attempt)
	{
		if (Db->Close())
		{
			break;
		}
		FPlatformProcess::Sleep(0.015f);
	}
	if (Db && Db->IsValid())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("UnrealAi vector SQLite: could not close DB after retries; leaking connection to avoid engine fatal (path=%s err=%s)"),
			*GetIndexDbPath(),
			*Db->GetLastError());
		Db = nullptr;
		return;
	}
	delete Db;
	Db = nullptr;
}

bool FUnrealAiVectorIndexStore::EnsureSchema(FString& OutError)
{
	if (!Db && !OpenDb(OutError))
	{
		return false;
	}
	const TCHAR* CreateTableSql = TEXT(
		"CREATE TABLE IF NOT EXISTS chunks ("
		"chunk_id TEXT PRIMARY KEY,"
		"project_id TEXT NOT NULL,"
		"source_path TEXT NOT NULL,"
		"source_hash TEXT NOT NULL,"
		"chunk_text TEXT NOT NULL,"
		"content_hash TEXT NOT NULL,"
		"embedding_json TEXT NOT NULL"
		");");
	if (!Db->Execute(CreateTableSql))
	{
		OutError = TEXT("Failed creating chunks table.");
		return false;
	}
	if (!Db->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_chunks_project ON chunks(project_id);")))
	{
		OutError = TEXT("Failed creating chunks project index.");
		return false;
	}
	if (!Db->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_chunks_source ON chunks(project_id, source_path);")))
	{
		OutError = TEXT("Failed creating chunks source index.");
		return false;
	}
	return true;
}

bool FUnrealAiVectorIndexStore::UpsertAllChunks(const TArray<FUnrealAiVectorChunkRow>& Chunks, FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}
	if (!Db->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		OutError = TEXT("Failed to begin transaction.");
		return false;
	}
	FSQLitePreparedStatement DeleteStmt;
	if (!DeleteStmt.Create(*Db,
		TEXT("DELETE FROM chunks WHERE project_id=?1;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		Db->Execute(TEXT("ROLLBACK;"));
		OutError = TEXT("Failed to create clear-chunks statement.");
		return false;
	}
	DeleteStmt.SetBindingValueByIndex(1, ProjectId);
	if (DeleteStmt.Step() != ESQLitePreparedStatementStepResult::Done)
	{
		Db->Execute(TEXT("ROLLBACK;"));
		OutError = TEXT("Failed to clear existing chunks.");
		return false;
	}

	FSQLitePreparedStatement InsertStmt;
	if (!InsertStmt.Create(*Db,
		TEXT("INSERT INTO chunks(chunk_id, project_id, source_path, source_hash, chunk_text, content_hash, embedding_json) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		Db->Execute(TEXT("ROLLBACK;"));
		OutError = TEXT("Failed to create insert statement.");
		return false;
	}

	for (const FUnrealAiVectorChunkRow& Row : Chunks)
	{
		FString EmbeddingJson;
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			Values.Reserve(Row.Embedding.Num());
			for (const float V : Row.Embedding)
			{
				Values.Add(MakeShared<FJsonValueNumber>(V));
			}
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetArrayField(TEXT("v"), Values);
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&EmbeddingJson);
			FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		}
		InsertStmt.Reset();
		InsertStmt.SetBindingValueByIndex(1, Row.ChunkId);
		InsertStmt.SetBindingValueByIndex(2, ProjectId);
		InsertStmt.SetBindingValueByIndex(3, Row.SourcePath);
		InsertStmt.SetBindingValueByIndex(4, Row.ContentHash);
		InsertStmt.SetBindingValueByIndex(5, Row.Text);
		InsertStmt.SetBindingValueByIndex(6, Row.ContentHash);
		InsertStmt.SetBindingValueByIndex(7, EmbeddingJson);
		if (InsertStmt.Step() != ESQLitePreparedStatementStepResult::Done)
		{
			Db->Execute(TEXT("ROLLBACK;"));
			OutError = TEXT("Insert chunk statement failed.");
			return false;
		}
	}

	if (!Db->Execute(TEXT("COMMIT;")))
	{
		OutError = TEXT("Failed to commit transaction.");
		return false;
	}
	return true;
}

bool FUnrealAiVectorIndexStore::LoadSourceHashes(TMap<FString, FString>& OutSourceHashes, FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	OutSourceHashes.Reset();
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}
	FSQLitePreparedStatement SelectStmt;
	if (!SelectStmt.Create(*Db,
		TEXT("SELECT source_path, source_hash FROM chunks WHERE project_id=?1 GROUP BY source_path, source_hash;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		OutError = TEXT("Failed to create source hash statement.");
		return false;
	}
	SelectStmt.SetBindingValueByIndex(1, ProjectId);
	while (SelectStmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FString SourcePath;
		FString SourceHash;
		SelectStmt.GetColumnValueByIndex(0, SourcePath);
		SelectStmt.GetColumnValueByIndex(1, SourceHash);
		if (!SourcePath.IsEmpty())
		{
			OutSourceHashes.Add(SourcePath, SourceHash);
		}
	}
	return true;
}

bool FUnrealAiVectorIndexStore::UpsertIncremental(
	const TMap<FString, FString>& SourceHashes,
	const TMap<FString, TArray<FUnrealAiVectorChunkRow>>& ChunksBySource,
	const TArray<FString>& RemovedSources,
	FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}
	if (!Db->Execute(TEXT("BEGIN TRANSACTION;")))
	{
		OutError = TEXT("Failed to begin incremental transaction.");
		return false;
	}

	auto DeleteSource = [this](const FString& SourcePath) -> bool
	{
		FSQLitePreparedStatement DeleteStmt;
		if (!DeleteStmt.Create(*Db,
			TEXT("DELETE FROM chunks WHERE project_id=?1 AND source_path=?2;"),
			ESQLitePreparedStatementFlags::Persistent))
		{
			return false;
		}
		DeleteStmt.SetBindingValueByIndex(1, ProjectId);
		DeleteStmt.SetBindingValueByIndex(2, SourcePath);
		return DeleteStmt.Step() == ESQLitePreparedStatementStepResult::Done;
	};

	for (const FString& SourcePath : RemovedSources)
	{
		if (!DeleteSource(SourcePath))
		{
			Db->Execute(TEXT("ROLLBACK;"));
			OutError = TEXT("Failed removing deleted source chunks.");
			return false;
		}
	}

	FSQLitePreparedStatement InsertStmt;
	if (!InsertStmt.Create(*Db,
		TEXT("INSERT INTO chunks(chunk_id, project_id, source_path, source_hash, chunk_text, content_hash, embedding_json) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		Db->Execute(TEXT("ROLLBACK;"));
		OutError = TEXT("Failed creating incremental insert statement.");
		return false;
	}

	for (const TPair<FString, TArray<FUnrealAiVectorChunkRow>>& Pair : ChunksBySource)
	{
		const FString& SourcePath = Pair.Key;
		if (!DeleteSource(SourcePath))
		{
			Db->Execute(TEXT("ROLLBACK;"));
			OutError = TEXT("Failed clearing changed source chunks.");
			return false;
		}
		const FString SourceHash = SourceHashes.FindRef(SourcePath);
		for (const FUnrealAiVectorChunkRow& Row : Pair.Value)
		{
			FString EmbeddingJson;
			{
				TArray<TSharedPtr<FJsonValue>> Values;
				for (const float V : Row.Embedding)
				{
					Values.Add(MakeShared<FJsonValueNumber>(V));
				}
				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetArrayField(TEXT("v"), Values);
				const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&EmbeddingJson);
				FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
			}
			InsertStmt.Reset();
			InsertStmt.SetBindingValueByIndex(1, Row.ChunkId);
			InsertStmt.SetBindingValueByIndex(2, ProjectId);
			InsertStmt.SetBindingValueByIndex(3, Row.SourcePath);
			InsertStmt.SetBindingValueByIndex(4, SourceHash);
			InsertStmt.SetBindingValueByIndex(5, Row.Text);
			InsertStmt.SetBindingValueByIndex(6, Row.ContentHash);
			InsertStmt.SetBindingValueByIndex(7, EmbeddingJson);
			if (InsertStmt.Step() != ESQLitePreparedStatementStepResult::Done)
			{
				Db->Execute(TEXT("ROLLBACK;"));
				OutError = TEXT("Failed inserting incremental source chunks.");
				return false;
			}
		}
	}

	if (!Db->Execute(TEXT("COMMIT;")))
	{
		OutError = TEXT("Failed to commit incremental transaction.");
		return false;
	}
	return true;
}

bool FUnrealAiVectorIndexStore::QueryTopKByCosine(
	const TArray<float>& QueryEmbedding,
	const int32 TopK,
	TArray<FUnrealAiRetrievalSnippet>& OutSnippets,
	FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	OutSnippets.Reset();
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}

	FSQLitePreparedStatement SelectStmt;
	if (!SelectStmt.Create(*Db,
		TEXT("SELECT chunk_id, source_path, chunk_text, embedding_json FROM chunks WHERE project_id=?1;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		OutError = TEXT("Failed to create select statement.");
		return false;
	}
	SelectStmt.SetBindingValueByIndex(1, ProjectId);

	TArray<FUnrealAiRetrievalSnippet> All;
	while (SelectStmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FString ChunkId;
		FString SourcePath;
		FString ChunkText;
		FString EmbeddingJson;
		SelectStmt.GetColumnValueByIndex(0, ChunkId);
		SelectStmt.GetColumnValueByIndex(1, SourcePath);
		SelectStmt.GetColumnValueByIndex(2, ChunkText);
		SelectStmt.GetColumnValueByIndex(3, EmbeddingJson);

		TSharedPtr<FJsonObject> EmbeddingObj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EmbeddingJson);
		if (!FJsonSerializer::Deserialize(Reader, EmbeddingObj) || !EmbeddingObj.IsValid())
		{
			continue;
		}
		const TArray<TSharedPtr<FJsonValue>>* VecArray = nullptr;
		if (!EmbeddingObj->TryGetArrayField(TEXT("v"), VecArray) || !VecArray)
		{
			continue;
		}
		TArray<float> Embedding;
		Embedding.Reserve(VecArray->Num());
		for (const TSharedPtr<FJsonValue>& V : *VecArray)
		{
			Embedding.Add(static_cast<float>(V.IsValid() ? V->AsNumber() : 0.0));
		}

		FUnrealAiRetrievalSnippet Snippet;
		Snippet.SnippetId = ChunkId;
		Snippet.SourceId = SourcePath;
		{
			const FString Marker = TEXT(":thread:");
			const int32 ThreadPos = SourcePath.Find(Marker, ESearchCase::CaseSensitive);
			if (ThreadPos != INDEX_NONE)
			{
				Snippet.ThreadId = SourcePath.Mid(ThreadPos + Marker.Len());
			}
		}
		Snippet.Text = ChunkText;
		Snippet.Score = CosineSimilarity(QueryEmbedding, Embedding);
		All.Add(MoveTemp(Snippet));
	}

	All.Sort([](const FUnrealAiRetrievalSnippet& A, const FUnrealAiRetrievalSnippet& B)
	{
		return A.Score > B.Score;
	});
	const int32 Keep = FMath::Clamp(TopK, 0, All.Num());
	for (int32 i = 0; i < Keep; ++i)
	{
		OutSnippets.Add(All[i]);
	}
	return true;
}

bool FUnrealAiVectorIndexStore::QueryTopKByLexical(
	const FString& QueryText,
	const int32 TopK,
	TArray<FUnrealAiRetrievalSnippet>& OutSnippets,
	FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	OutSnippets.Reset();
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}

	FString Clean = QueryText.ToLower();
	for (int32 i = 0; i < Clean.Len(); ++i)
	{
		const TCHAR C = Clean[i];
		if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('/') && C != TEXT('.'))
		{
			Clean[i] = TEXT(' ');
		}
	}
	TArray<FString> Tokens;
	Clean.ParseIntoArray(Tokens, TEXT(" "), true);
	TArray<FString> Keywords;
	TSet<FString> Seen;
	for (const FString& T : Tokens)
	{
		if (T.Len() < 3 || Seen.Contains(T))
		{
			continue;
		}
		Seen.Add(T);
		Keywords.Add(T);
	}
	if (Keywords.Num() == 0)
	{
		return true;
	}

	FSQLitePreparedStatement SelectStmt;
	if (!SelectStmt.Create(*Db,
		TEXT("SELECT chunk_id, source_path, chunk_text FROM chunks WHERE project_id=?1;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		OutError = TEXT("Failed to create lexical select statement.");
		return false;
	}
	SelectStmt.SetBindingValueByIndex(1, ProjectId);

	TArray<FUnrealAiRetrievalSnippet> All;
	while (SelectStmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FString ChunkId;
		FString SourcePath;
		FString ChunkText;
		SelectStmt.GetColumnValueByIndex(0, ChunkId);
		SelectStmt.GetColumnValueByIndex(1, SourcePath);
		SelectStmt.GetColumnValueByIndex(2, ChunkText);
		if (ChunkText.IsEmpty())
		{
			continue;
		}
		const FString ChunkLower = ChunkText.ToLower();
		int32 Hits = 0;
		for (const FString& K : Keywords)
		{
			if (ChunkLower.Contains(K))
			{
				++Hits;
			}
		}
		if (Hits <= 0)
		{
			continue;
		}
		FUnrealAiRetrievalSnippet Snippet;
		Snippet.SnippetId = ChunkId;
		Snippet.SourceId = SourcePath;
		{
			const FString Marker = TEXT(":thread:");
			const int32 ThreadPos = SourcePath.Find(Marker, ESearchCase::CaseSensitive);
			if (ThreadPos != INDEX_NONE)
			{
				Snippet.ThreadId = SourcePath.Mid(ThreadPos + Marker.Len());
			}
		}
		Snippet.Text = ChunkText;
		Snippet.Score = static_cast<float>(Hits) / static_cast<float>(Keywords.Num());
		All.Add(MoveTemp(Snippet));
	}

	All.Sort([](const FUnrealAiRetrievalSnippet& A, const FUnrealAiRetrievalSnippet& B)
	{
		if (!FMath::IsNearlyEqual(A.Score, B.Score))
		{
			return A.Score > B.Score;
		}
		return A.Text.Len() < B.Text.Len();
	});
	const int32 Keep = FMath::Clamp(TopK, 0, All.Num());
	for (int32 i = 0; i < Keep; ++i)
	{
		OutSnippets.Add(All[i]);
	}
	return true;
}

bool FUnrealAiVectorIndexStore::GetIndexCounts(int32& OutFiles, int32& OutChunks, FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	OutFiles = 0;
	OutChunks = 0;
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}
	FSQLitePreparedStatement C1;
	if (!C1.Create(*Db,
		TEXT("SELECT COUNT(DISTINCT source_path) FROM chunks WHERE project_id=?1;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		OutError = TEXT("Failed to create file count statement.");
		return false;
	}
	C1.SetBindingValueByIndex(1, ProjectId);
	if (C1.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		C1.GetColumnValueByIndex(0, OutFiles);
	}
	FSQLitePreparedStatement C2;
	if (!C2.Create(*Db,
		TEXT("SELECT COUNT(*) FROM chunks WHERE project_id=?1;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		OutError = TEXT("Failed to create chunk count statement.");
		return false;
	}
	C2.SetBindingValueByIndex(1, ProjectId);
	if (C2.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		C2.GetColumnValueByIndex(0, OutChunks);
	}
	return true;
}

namespace UnrealAiVectorIndexStorePriv
{
	static bool IsEmbeddingJsonUnusable(const FString& EmbeddingJson)
	{
		if (EmbeddingJson.IsEmpty())
		{
			return true;
		}
		TSharedPtr<FJsonObject> EmbeddingObj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EmbeddingJson);
		if (!FJsonSerializer::Deserialize(Reader, EmbeddingObj) || !EmbeddingObj.IsValid())
		{
			return true;
		}
		const TArray<TSharedPtr<FJsonValue>>* VecArray = nullptr;
		if (!EmbeddingObj->TryGetArrayField(TEXT("v"), VecArray) || !VecArray || VecArray->Num() == 0)
		{
			return true;
		}
		return false;
	}
}

bool FUnrealAiVectorIndexStore::CountChunksWithUnusableEmbeddings(int32& OutCount, FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	OutCount = 0;
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}
	FSQLitePreparedStatement SelectStmt;
	if (!SelectStmt.Create(
		*Db,
		TEXT("SELECT embedding_json FROM chunks WHERE project_id=?1;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		OutError = TEXT("Failed to create embedding scan statement.");
		return false;
	}
	SelectStmt.SetBindingValueByIndex(1, ProjectId);
	while (SelectStmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FString EmbeddingJson;
		SelectStmt.GetColumnValueByIndex(0, EmbeddingJson);
		if (UnrealAiVectorIndexStorePriv::IsEmbeddingJsonUnusable(EmbeddingJson))
		{
			++OutCount;
		}
	}
	return true;
}

bool FUnrealAiVectorIndexStore::DeleteChunksWithUnusableEmbeddings(int32& OutDeleted, FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	OutDeleted = 0;
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}
	TArray<FString> BadChunkIds;
	{
		FSQLitePreparedStatement SelectStmt;
		if (!SelectStmt.Create(
			*Db,
			TEXT("SELECT chunk_id, embedding_json FROM chunks WHERE project_id=?1;"),
			ESQLitePreparedStatementFlags::Persistent))
		{
			OutError = TEXT("Failed to create embedding/chunk_id scan statement.");
			return false;
		}
		SelectStmt.SetBindingValueByIndex(1, ProjectId);
		while (SelectStmt.Step() == ESQLitePreparedStatementStepResult::Row)
		{
			FString ChunkId;
			FString EmbeddingJson;
			SelectStmt.GetColumnValueByIndex(0, ChunkId);
			SelectStmt.GetColumnValueByIndex(1, EmbeddingJson);
			if (ChunkId.IsEmpty())
			{
				continue;
			}
			if (UnrealAiVectorIndexStorePriv::IsEmbeddingJsonUnusable(EmbeddingJson))
			{
				BadChunkIds.Add(MoveTemp(ChunkId));
			}
		}
	}
	if (BadChunkIds.Num() == 0)
	{
		return true;
	}
	if (!Db->Execute(TEXT("BEGIN IMMEDIATE;")))
	{
		OutError = TEXT("Failed to begin unusable-embedding delete transaction.");
		return false;
	}
	FSQLitePreparedStatement DelStmt;
	if (!DelStmt.Create(
		*Db,
		TEXT("DELETE FROM chunks WHERE project_id=?1 AND chunk_id=?2;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		Db->Execute(TEXT("ROLLBACK;"));
		OutError = TEXT("Failed to create per-chunk delete statement.");
		return false;
	}
	for (const FString& Id : BadChunkIds)
	{
		DelStmt.Reset();
		DelStmt.SetBindingValueByIndex(1, ProjectId);
		DelStmt.SetBindingValueByIndex(2, Id);
		if (DelStmt.Step() != ESQLitePreparedStatementStepResult::Done)
		{
			Db->Execute(TEXT("ROLLBACK;"));
			OutError = TEXT("Failed deleting a chunk with unusable embedding.");
			return false;
		}
		++OutDeleted;
	}
	if (!Db->Execute(TEXT("COMMIT;")))
	{
		OutError = TEXT("Failed to commit unusable-embedding deletes.");
		return false;
	}
	return true;
}

bool FUnrealAiVectorIndexStore::DeleteAllChunksForProject(FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}
	if (!Db->Execute(TEXT("BEGIN IMMEDIATE;")))
	{
		OutError = TEXT("Failed to begin wipe transaction.");
		return false;
	}
	FSQLitePreparedStatement DelStmt;
	if (!DelStmt.Create(
		*Db,
		TEXT("DELETE FROM chunks WHERE project_id=?1;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		Db->Execute(TEXT("ROLLBACK;"));
		OutError = TEXT("Failed to create chunk delete statement.");
		return false;
	}
	DelStmt.SetBindingValueByIndex(1, ProjectId);
	if (DelStmt.Step() != ESQLitePreparedStatementStepResult::Done)
	{
		Db->Execute(TEXT("ROLLBACK;"));
		OutError = TEXT("Failed deleting project chunks.");
		return false;
	}
	if (!Db->Execute(TEXT("COMMIT;")))
	{
		OutError = TEXT("Failed to commit chunk wipe.");
		return false;
	}
	return true;
}

bool FUnrealAiVectorIndexStore::CheckIntegrity(FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}
	FSQLitePreparedStatement CheckStmt;
	if (!CheckStmt.Create(*Db, TEXT("PRAGMA quick_check;"), ESQLitePreparedStatementFlags::Persistent))
	{
		OutError = TEXT("Failed to run PRAGMA quick_check.");
		return false;
	}
	if (CheckStmt.Step() != ESQLitePreparedStatementStepResult::Row)
	{
		OutError = TEXT("Integrity check returned no row.");
		return false;
	}
	FString ResultText;
	CheckStmt.GetColumnValueByIndex(0, ResultText);
	if (!ResultText.Equals(TEXT("ok"), ESearchCase::IgnoreCase))
	{
		OutError = FString::Printf(TEXT("SQLite integrity failed: %s"), *ResultText);
		return false;
	}
	return true;
}

bool FUnrealAiVectorIndexStore::GetTopSourcesByChunkCount_NoLock(
	const int32 Limit,
	TArray<TPair<FString, int32>>& OutRows,
	FString& OutError)
{
	OutRows.Reset();
	FSQLitePreparedStatement Stmt;
	if (!Stmt.Create(
		*Db,
		TEXT("SELECT source_path, COUNT(*) AS cnt FROM chunks WHERE project_id=?1 GROUP BY source_path ORDER BY cnt DESC LIMIT ?2;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		OutError = TEXT("Failed to create top sources statement.");
		return false;
	}
	Stmt.SetBindingValueByIndex(1, ProjectId);
	Stmt.SetBindingValueByIndex(2, FMath::Clamp(Limit, 0, 1000));
	while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FString Source;
		int32 Count = 0;
		Stmt.GetColumnValueByIndex(0, Source);
		Stmt.GetColumnValueByIndex(1, Count);
		OutRows.Add(TPair<FString, int32>(Source, Count));
	}
	return true;
}

bool FUnrealAiVectorIndexStore::GetTopSourcesByChunkCount(const int32 Limit, TArray<TPair<FString, int32>>& OutRows, FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}
	return GetTopSourcesByChunkCount_NoLock(Limit, OutRows, OutError);
}

bool FUnrealAiVectorIndexStore::GetTopSourcesByChunkCountWithSamples(
	const int32 Limit,
	const int32 SamplePerSource,
	TArray<FUnrealAiVectorDbTopSourceRow>& OutRows,
	FString& OutError)
{
	FScopeLock Lock(&DbMutex);
	OutRows.Reset();
	if (!EnsureInitializedUnlocked(OutError))
	{
		return false;
	}

	TArray<TPair<FString, int32>> Top;
	if (!GetTopSourcesByChunkCount_NoLock(Limit, Top, OutError))
	{
		return false;
	}

	const int32 KeepPerSource = FMath::Clamp(SamplePerSource, 0, 20);
	const int32 MaxChunkCharsForUi = 2200;

	FSQLitePreparedStatement SampleStmt;
	if (!SampleStmt.Create(
		*Db,
		TEXT("SELECT chunk_id, chunk_text FROM chunks WHERE project_id=?1 AND source_path=?2 LIMIT ?3;"),
		ESQLitePreparedStatementFlags::Persistent))
	{
		OutError = TEXT("Failed to create sample chunks statement.");
		return false;
	}

	for (const TPair<FString, int32>& P : Top)
	{
		const FString& SourcePath = P.Key;
		const int32 ChunkCount = P.Value;
		if (SourcePath.IsEmpty())
		{
			continue;
		}

		FUnrealAiVectorDbTopSourceRow Row;
		Row.SourcePath = SourcePath;
		Row.ChunkCount = ChunkCount;

		{
			const FString Marker = TEXT(":thread:");
			const int32 ThreadPos = SourcePath.Find(Marker, ESearchCase::CaseSensitive);
			if (ThreadPos != INDEX_NONE)
			{
				Row.ThreadIdHint = SourcePath.Mid(ThreadPos + Marker.Len());
			}
		}

		Row.ChunkSamples.Reset();
		if (KeepPerSource > 0)
		{
			SampleStmt.Reset();
			SampleStmt.SetBindingValueByIndex(1, ProjectId);
			SampleStmt.SetBindingValueByIndex(2, SourcePath);
			SampleStmt.SetBindingValueByIndex(3, KeepPerSource);

			while (SampleStmt.Step() == ESQLitePreparedStatementStepResult::Row)
			{
				FUnrealAiVectorDbTopChunkRow C;
				SampleStmt.GetColumnValueByIndex(0, C.ChunkId);
				SampleStmt.GetColumnValueByIndex(1, C.ChunkText);
				if (!C.ChunkText.IsEmpty() && C.ChunkText.Len() > MaxChunkCharsForUi)
				{
					C.ChunkText = C.ChunkText.Left(MaxChunkCharsForUi) + TEXT(" …");
				}
				if (!C.ChunkId.IsEmpty() || !C.ChunkText.IsEmpty())
				{
					Row.ChunkSamples.Add(MoveTemp(C));
				}
			}
		}

		OutRows.Add(MoveTemp(Row));
	}

	return true;
}

bool FUnrealAiVectorIndexStore::LoadManifest(FUnrealAiVectorManifest& OutManifest) const
{
	OutManifest = FUnrealAiVectorManifest();
	OutManifest.ProjectId = ProjectId;
	const FString Path = GetManifestPath();
	FString Json;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Json, *Path))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	Root->TryGetStringField(TEXT("project_id"), OutManifest.ProjectId);
	double Number = 0.0;
	if (Root->TryGetNumberField(TEXT("index_version"), Number))
	{
		OutManifest.IndexVersion = static_cast<int32>(Number);
	}
	Root->TryGetStringField(TEXT("embedding_model"), OutManifest.EmbeddingModel);
	Root->TryGetStringField(TEXT("status"), OutManifest.Status);
	Root->TryGetStringField(TEXT("migration_state"), OutManifest.MigrationState);
	if (Root->TryGetNumberField(TEXT("files_indexed"), Number))
	{
		OutManifest.FilesIndexed = static_cast<int32>(Number);
	}
	if (Root->TryGetNumberField(TEXT("chunks_indexed"), Number))
	{
		OutManifest.ChunksIndexed = static_cast<int32>(Number);
	}
	if (Root->TryGetNumberField(TEXT("index_build_target_chunks"), Number))
	{
		OutManifest.IndexBuildTargetChunks = static_cast<int32>(Number);
	}
	if (Root->TryGetNumberField(TEXT("index_build_completed_chunks"), Number))
	{
		OutManifest.IndexBuildCompletedChunks = static_cast<int32>(Number);
	}
	if (Root->TryGetNumberField(TEXT("index_build_wave_total"), Number))
	{
		OutManifest.IndexBuildWaveTotal = static_cast<int32>(Number);
	}
	if (Root->TryGetNumberField(TEXT("index_build_wave_done"), Number))
	{
		OutManifest.IndexBuildWaveDone = static_cast<int32>(Number);
	}
	FString PhaseStartedIso;
	if (Root->TryGetStringField(TEXT("index_build_phase_started_utc"), PhaseStartedIso))
	{
		FDateTime::ParseIso8601(*PhaseStartedIso, OutManifest.IndexBuildPhaseStartedUtc);
	}
	if (Root->TryGetNumberField(TEXT("pending_dirty_count"), Number))
	{
		OutManifest.PendingDirtyCount = static_cast<int32>(Number);
	}
	FString DateStr;
	if (Root->TryGetStringField(TEXT("last_full_scan_utc"), DateStr))
	{
		FDateTime::ParseIso8601(*DateStr, OutManifest.LastFullScanUtc);
	}
	if (Root->TryGetStringField(TEXT("last_incremental_scan_utc"), DateStr))
	{
		FDateTime::ParseIso8601(*DateStr, OutManifest.LastIncrementalScanUtc);
	}
	FString RetryIso;
	if (Root->TryGetStringField(TEXT("vector_db_open_retry_not_before_utc"), RetryIso))
	{
		FDateTime::ParseIso8601(*RetryIso, OutManifest.VectorDbOpenRetryNotBeforeUtc);
	}
	return true;
}

bool FUnrealAiVectorIndexStore::SaveManifest(const FUnrealAiVectorManifest& Manifest) const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("project_id"), Manifest.ProjectId);
	Root->SetNumberField(TEXT("index_version"), Manifest.IndexVersion);
	Root->SetStringField(TEXT("embedding_model"), Manifest.EmbeddingModel);
	Root->SetStringField(TEXT("last_full_scan_utc"), Manifest.LastFullScanUtc.ToIso8601());
	Root->SetStringField(TEXT("last_incremental_scan_utc"), Manifest.LastIncrementalScanUtc.ToIso8601());
	Root->SetNumberField(TEXT("files_indexed"), Manifest.FilesIndexed);
	Root->SetNumberField(TEXT("chunks_indexed"), Manifest.ChunksIndexed);
	Root->SetNumberField(TEXT("index_build_target_chunks"), Manifest.IndexBuildTargetChunks);
	Root->SetNumberField(TEXT("index_build_completed_chunks"), Manifest.IndexBuildCompletedChunks);
	Root->SetNumberField(TEXT("index_build_wave_total"), Manifest.IndexBuildWaveTotal);
	Root->SetNumberField(TEXT("index_build_wave_done"), Manifest.IndexBuildWaveDone);
	if (Manifest.IndexBuildPhaseStartedUtc > FDateTime::MinValue())
	{
		Root->SetStringField(TEXT("index_build_phase_started_utc"), Manifest.IndexBuildPhaseStartedUtc.ToIso8601());
	}
	Root->SetNumberField(TEXT("pending_dirty_count"), Manifest.PendingDirtyCount);
	Root->SetStringField(TEXT("status"), Manifest.Status);
	Root->SetStringField(TEXT("migration_state"), Manifest.MigrationState);
	if (Manifest.VectorDbOpenRetryNotBeforeUtc != FDateTime::MinValue())
	{
		Root->SetStringField(TEXT("vector_db_open_retry_not_before_utc"), Manifest.VectorDbOpenRetryNotBeforeUtc.ToIso8601());
	}
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*RootDir, true);
	return FFileHelper::SaveStringToFile(Json, *GetManifestPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FString FUnrealAiVectorIndexStore::GetIndexDbPath() const
{
	return FPaths::Combine(RootDir, TEXT("index.db"));
}

FString FUnrealAiVectorIndexStore::GetManifestPath() const
{
	return FPaths::Combine(RootDir, TEXT("manifest.json"));
}
