#pragma once

#include "CoreMinimal.h"
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
	int32 PendingDirtyCount = 0;
	FString Status = TEXT("stale");
	FString MigrationState = TEXT("none");
	FDateTime LastIncrementalScanUtc = FDateTime::MinValue();
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
	bool CheckIntegrity(FString& OutError);
	bool GetTopSourcesByChunkCount(int32 Limit, TArray<TPair<FString, int32>>& OutRows, FString& OutError);

	bool LoadManifest(FUnrealAiVectorManifest& OutManifest) const;
	bool SaveManifest(const FUnrealAiVectorManifest& Manifest) const;

	FString GetIndexDbPath() const;
	FString GetManifestPath() const;

private:
	bool EnsureSchema(FString& OutError);
	bool OpenDb(FString& OutError);
	void CloseDb();

	FString ProjectId;
	FString RootDir;
	mutable class FSQLiteDatabase* Db = nullptr;
};
