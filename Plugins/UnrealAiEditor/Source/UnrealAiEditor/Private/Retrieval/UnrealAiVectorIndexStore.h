#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Retrieval/UnrealAiRetrievalTypes.h"

struct FUnrealAiVectorChunkRow
{
	FString ChunkId;
	FString SourcePath;
	FString Text;
	FString ContentHash;
	TArray<float> Embedding;
};

struct FUnrealAiVectorManifest
{
	FString ProjectId;
	int32 IndexVersion = 1;
	FString EmbeddingModel;
	FDateTime LastFullScanUtc = FDateTime::MinValue();
	int32 FilesIndexed = 0;
	int32 ChunksIndexed = 0;
	/** During indexing, total chunks in the embedding phase (0 = not in embed phase / unknown). */
	int32 IndexBuildTargetChunks = 0;
	/** Chunks embedded so far in the current indexing run. */
	int32 IndexBuildCompletedChunks = 0;
	/** Total phased waves (e.g. 5); 0 = unset / legacy manifest. */
	int32 IndexBuildWaveTotal = 0;
	/** Waves fully committed to SQLite (0..IndexBuildWaveTotal). */
	int32 IndexBuildWaveDone = 0;
	/** UTC when the embedding phase began (for ETA; MinValue when unused). */
	FDateTime IndexBuildPhaseStartedUtc = FDateTime::MinValue();
	int32 PendingDirtyCount = 0;
	FString Status = TEXT("stale");
	FString MigrationState = TEXT("none");
	FDateTime LastIncrementalScanUtc = FDateTime::MinValue();
	/** When set in the future, automatic index rebuild / aggressive open retries back off until this time. */
	FDateTime VectorDbOpenRetryNotBeforeUtc = FDateTime::MinValue();
};

class FUnrealAiVectorIndexStore
{
public:
	explicit FUnrealAiVectorIndexStore(const FString& InProjectId);
	~FUnrealAiVectorIndexStore();

	bool Initialize(FString& OutError);
	bool UpsertAllChunks(const TArray<FUnrealAiVectorChunkRow>& Chunks, FString& OutError);
	bool UpsertIncremental(
		const TMap<FString, FString>& SourceHashes,
		const TMap<FString, TArray<FUnrealAiVectorChunkRow>>& ChunksBySource,
		const TArray<FString>& RemovedSources,
		FString& OutError);
	bool QueryTopKByCosine(const TArray<float>& QueryEmbedding, int32 TopK, TArray<FUnrealAiRetrievalSnippet>& OutSnippets, FString& OutError);
	bool QueryTopKByLexical(const FString& QueryText, int32 TopK, TArray<FUnrealAiRetrievalSnippet>& OutSnippets, FString& OutError);
	bool LoadSourceHashes(TMap<FString, FString>& OutSourceHashes, FString& OutError);
	bool GetIndexCounts(int32& OutFiles, int32& OutChunks, FString& OutError);
	/** Rows with missing / invalid embedding JSON or an empty "v" vector (not usable for cosine retrieval). */
	bool CountChunksWithUnusableEmbeddings(int32& OutCount, FString& OutError);
	/** Deletes only rows with unusable embeddings (preserves the rest of the index). */
	bool DeleteChunksWithUnusableEmbeddings(int32& OutDeleted, FString& OutError);
	/** Deletes all chunk rows for this store's project_id (schema preserved). */
	bool DeleteAllChunksForProject(FString& OutError);
	bool CheckIntegrity(FString& OutError);
	bool GetTopSourcesByChunkCount(int32 Limit, TArray<TPair<FString, int32>>& OutRows, FString& OutError);
	bool GetTopSourcesByChunkCountWithSamples(
		int32 Limit,
		int32 SamplePerSource,
		TArray<FUnrealAiVectorDbTopSourceRow>& OutRows,
		FString& OutError);

	bool LoadManifest(FUnrealAiVectorManifest& OutManifest) const;
	bool SaveManifest(const FUnrealAiVectorManifest& Manifest) const;

	FString GetIndexDbPath() const;
	FString GetManifestPath() const;

private:
	bool EnsureInitializedUnlocked(FString& OutError);
	bool EnsureSchema(FString& OutError);
	bool OpenDb(FString& OutError);
	void CloseDb();
	void CloseDbUnlocked();
	bool GetTopSourcesByChunkCount_NoLock(int32 Limit, TArray<TPair<FString, int32>>& OutRows, FString& OutError);

	FString ProjectId;
	FString RootDir;
	mutable FCriticalSection DbMutex;
	mutable class FSQLiteDatabase* Db = nullptr;
};
