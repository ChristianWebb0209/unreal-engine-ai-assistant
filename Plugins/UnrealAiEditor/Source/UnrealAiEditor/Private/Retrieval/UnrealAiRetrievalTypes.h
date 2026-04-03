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
	/** 0 = unlimited. Caps how many chunks may be embedded in one rebuild (same units as maxTotalChunksPerRebuild). */
	int32 MaxEmbeddingCallsPerRebuild = 0;

	/** 0 = unlimited. Caps fixed-window chunks per filesystem / virtual text source (limits giant files). */
	int32 MaxChunksPerFile = 96;

	int32 ChunkChars = 4000;
	int32 ChunkOverlap = 150;

	/** 0 = skip Asset Registry synthetic corpus (legacy default: off). */
	int32 AssetRegistryMaxAssets = 0;
	/** When true, include assets under /Engine in the registry snapshot (can be large). */
	bool bAssetRegistryIncludeEngineAssets = false;

	/** When true, include /Engine Blueprints in metadata indexing (default: /Game only). */
	bool bBlueprintIncludeEngineAssets = false;

	/** Virtual corpus: L0/L1 directory listing chunks (small). */
	bool bIndexDirectorySummaries = true;
	/** Virtual corpus: grouped asset path summaries under /Game and /Engine. */
	bool bIndexAssetFamilySummaries = true;
	/** Virtual corpus: editor world actor dump (can be large; off by default). */
	bool bIndexEditorScene = false;

	/** Chunks per OpenAI-style `/embeddings` HTTP request during index builds (provider max is often 2048 inputs; indexer clamps to 512). */
	int32 EmbeddingBatchSize = 128;
	int32 MinDelayMsBetweenEmbeddingBatches = 0;

	/** 0 = off. When > 0, wave-P0 may flush completed sources mid-embed after this many seconds (best-effort). */
	int32 IndexFirstWaveTimeBudgetSeconds = 0;

	/**
	 * When true, all five embedding waves run, including wave 4 (virtual corpus shards/summaries,
	 * /Game blueprint paths, memory keys, asset-registry virtual paths, and other catch-all sources).
	 * When false (default), only waves 0–3 run (Source/Config/docs/Content tiers); existing wave-4 rows are removed on rebuild.
	 */
	bool bIndexRetrievalWave4 = false;

	/**
	 * When false (default), memory records are not embedded into the vector index.
	 * The tagged memory system remains the primary memory UX; see docs/context/memory-system.md.
	 */
	bool bIndexMemoryRecordsInVectorStore = false;

	/** 0 = use internal default cap (~800). Set higher for large projects. */
	int32 BlueprintMaxFeatureRecords = 0;
	/** Context packer aggression [0,1], default 0.5. */
	float ContextAggression = 0.5f;
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
	int32 IndexBuildTargetChunks = 0;
	int32 IndexBuildCompletedChunks = 0;
	/** Manifest phased-build progress; 0 when not tracking waves. */
	int32 IndexBuildWaveTotal = 0;
	int32 IndexBuildWaveDone = 0;
	FDateTime IndexBuildPhaseStartedUtc = FDateTime::MinValue();
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

/** Single source node in the vector-db top-N visualization. */
struct FUnrealAiVectorDbTopChunkRow
{
	FString ChunkId;
	FString ChunkText;
};

struct FUnrealAiVectorDbTopSourceRow
{
	/** DB source_path (often includes tool/thread markers). */
	FString SourcePath;
	/** Total number of indexed chunks for this source_path. */
	int32 ChunkCount = 0;
	/** Optional marker parsed from source_path (text after `:thread:`), when present. */
	FString ThreadIdHint;
	/** Small sample of chunk rows to render in the drill-down pane. */
	TArray<FUnrealAiVectorDbTopChunkRow> ChunkSamples;
};
