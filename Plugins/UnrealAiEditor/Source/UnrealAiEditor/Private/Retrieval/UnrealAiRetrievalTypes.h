#pragma once

#include "CoreMinimal.h"

enum class EUnrealAiRetrievalRootPreset : uint8
{
	/** Source/, docs/ only (legacy default). */
	Minimal = 0,
	/** Minimal plus Config and each Plugins subfolder Source tree. */
	StandardRoots = 1,
	/** StandardRoots plus Content (whitelist filters extensions only). */
	ExtendedRoots = 2,
};

struct FUnrealAiRetrievalSettings
{
	bool bEnabled = false;
	FString EmbeddingModel;
	int32 MaxSnippetsPerTurn = 6;
	int32 MaxSnippetTokens = 256;
	bool bAutoIndexOnProjectOpen = true;
	int32 PeriodicScrubMinutes = 30;
	bool bAllowMixedModelCompatibility = false;

	/** When empty, a built-in legacy whitelist is used (.h, .hpp, .cpp, .md, .txt, .ini). */
	TArray<FString> IndexedExtensions;

	EUnrealAiRetrievalRootPreset RootPreset = EUnrealAiRetrievalRootPreset::Minimal;

	/** 0 = unlimited. */
	int32 MaxFilesPerRebuild = 0;
	/** 0 = unlimited. Counts chunks queued for embedding in one rebuild. */
	int32 MaxTotalChunksPerRebuild = 0;
	/** 0 = unlimited. Same as chunks when using one embedding per chunk. */
	int32 MaxEmbeddingCallsPerRebuild = 0;

	int32 ChunkChars = 1200;
	int32 ChunkOverlap = 200;

	/** 0 = skip Asset Registry synthetic corpus (legacy default: off). */
	int32 AssetRegistryMaxAssets = 0;
	/** When true, include assets under /Engine in the registry snapshot (can be large). */
	bool bAssetRegistryIncludeEngineAssets = false;

	int32 EmbeddingBatchSize = 8;
	int32 MinDelayMsBetweenEmbeddingBatches = 0;

	/**
	 * When false (default), memory records are not embedded into the vector index.
	 * The tagged memory system remains the primary memory UX; see docs/memory-system.md.
	 */
	bool bIndexMemoryRecordsInVectorStore = false;

	/** 0 = use extractor default cap (~4000). */
	int32 BlueprintMaxFeatureRecords = 0;
};

struct FUnrealAiRetrievalQuery
{
	FString ProjectId;
	FString ThreadId;
	FString QueryText;
	int32 MaxResults = 0;
};

struct FUnrealAiRetrievalSnippet
{
	FString SnippetId;
	FString SourceId;
	FString ThreadId;
	FString Text;
	float Score = 0.0f;
};

struct FUnrealAiRetrievalQueryResult
{
	TArray<FUnrealAiRetrievalSnippet> Snippets;
	TArray<FString> Warnings;
};

struct FUnrealAiRetrievalProjectStatus
{
	bool bEnabled = false;
	FString StateText = TEXT("disabled");
	int32 FilesIndexed = 0;
	int32 ChunksIndexed = 0;
	bool bBusy = false;
};

/** Snapshot for settings UI / diagnostics: policy summary, store paths, manifest, integrity, top sources. */
struct FUnrealAiRetrievalVectorDbOverview
{
	bool bRetrievalEnabledInSettings = false;
	FString RootPresetLabel;
	int32 IndexedExtensionCount = 0;
	FString IndexedExtensionsPreview;
	bool bAssetRegistryCorpusEnabled = false;
	bool bMemoryCorpusEnabled = false;
	bool bBlueprintCapCustom = false;
	int32 BlueprintMaxFeatureRecords = 0;

	bool bStoreAvailable = false;
	FString StoreError;
	FString IndexDbPath;
	FString ManifestPath;
	FString ManifestStatus;
	FString ManifestEmbeddingModel;
	FString MigrationState;
	int32 FilesIndexed = 0;
	int32 ChunksIndexed = 0;
	bool bIndexBusy = false;
	FDateTime LastFullScanUtc = FDateTime::MinValue();
	bool bIntegrityOk = true;
	FString IntegrityError;
	TArray<TPair<FString, int32>> TopSourcesByChunkCount;
};
