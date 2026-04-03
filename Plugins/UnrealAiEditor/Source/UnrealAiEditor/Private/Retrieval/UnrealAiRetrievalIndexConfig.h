#pragma once

#include "CoreMinimal.h"
#include "Retrieval/UnrealAiRetrievalTypes.h"
#include "Retrieval/UnrealAiVectorIndexStore.h"

namespace UnrealAiRetrievalIndexConfig
{
	/** Legacy extensions matching pre-whitelist behavior (lowercase, leading dot). */
	TArray<FString> GetLegacyDefaultIndexedExtensions();

	/** Normalized extensions: lowercase, leading dot. Empty entries dropped. */
	void NormalizeIndexedExtensions(TArray<FString>& InOutExtensions);

	/** Effective whitelist: from settings or legacy default when IndexedExtensions is empty. */
	void GetEffectiveIndexedExtensions(const FUnrealAiRetrievalSettings& Settings, TArray<FString>& OutExtLowerWithDot);

	/** Build absolute root directories to scan for filesystem corpus (must exist). */
	void AppendIndexRootsForPreset(const FString& ProjectDirAbs, EUnrealAiRetrievalRootPreset Preset, TArray<FString>& OutRoots);

	/**
	 * Enumerate indexable file paths under roots, whitelist-only, sorted, capped by MaxFilesPerRebuild.
	 * SkippedFilesOut counts files not indexed due to file cap (when MaxFiles > 0).
	 */
	void CollectFilesystemIndexPaths(
		const FString& ProjectDirAbs,
		const FUnrealAiRetrievalSettings& Settings,
		TArray<FString>& OutAbsolutePathsSorted,
		int32& OutSkippedFilesDueToCap);

	/**
	 * Stable ordering for index builds: project Source/, plugin Source/, Config, docs, Content, then other roots.
	 * Improves time-to-useful-results when combined with incremental hashing (same total work, better prioritization).
	 */
	void SortFilesystemIndexPathsForBuildPriority(const FString& ProjectDirAbs, TArray<FString>& InOutAbsolutePaths);

	/** Fixed-size window chunking with optional per-source cap (0 = unlimited). ChunkIds match retrieval service conventions. */
	void ChunkTextFixedWindow(
		const FString& RelativePath,
		const FString& Text,
		int32 ChunkChars,
		int32 OverlapChars,
		int32 MaxChunksPerSource,
		TArray<FUnrealAiVectorChunkRow>& OutChunks);

	/** Number of index waves (P0..P4). */
	int32 GetIndexBuildWaveCount();

	/**
	 * Priority wave for phased embedding/commits [0, GetIndexBuildWaveCount()).
	 * P0: project Source; P1: plugin Source; P2: Config + docs; P3: Content; P4: virtual + assets + other.
	 */
	int32 GetIndexBuildWaveForSource(const FString& ProjectDirAbs, const FString& SourceKey);
}
