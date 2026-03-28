#pragma once

#include "CoreMinimal.h"
#include "Retrieval/UnrealAiRetrievalTypes.h"

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
}
