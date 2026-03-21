#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

enum class EUnrealAiMentionKind : uint8
{
	Actor = 0,
	Asset = 1,
};

/** One row for @ autocomplete (content asset or level actor). */
struct FMentionCandidate
{
	EUnrealAiMentionKind Kind = EUnrealAiMentionKind::Asset;
	FString DisplayName;
	FString SortClass;
	/** Soft object path for assets; actor GetPathName() for actors. */
	FString PrimaryKey;
	FAssetData AssetData;

	bool IsAsset() const { return Kind == EUnrealAiMentionKind::Asset; }
	bool IsActor() const { return Kind == EUnrealAiMentionKind::Actor; }
};

/** Cached mention index: assets (async build + optional disk cache) + actors (refreshed from editor world). */
class FUnrealAiComposerMentionIndex
{
public:
	static FUnrealAiComposerMentionIndex& Get();

	/** Call from game thread. Starts background asset index build if needed. */
	void EnsureAssetIndexBuilding();

	/** True once asset list is ready (or failed empty). */
	bool AreAssetsReady() const { return bAssetIndexReady; }

	/** True while initial asset scan is in progress. */
	bool IsBuildingAssets() const { return bAssetIndexBuilding; }

	/** Filter + sort candidates. Token may be empty (show top results). Game thread only. */
	void FilterCandidates(const FString& Token, TArray<FMentionCandidate>& Out, int32 MaxResults = 40) const;

	/** Invalidate asset cache (e.g. registry changed). */
	void InvalidateAssetIndex();

private:
	FUnrealAiComposerMentionIndex();
	void TryLoadAssetCacheFromDisk();
	void SaveAssetCacheToDisk() const;
	void BuildAssetsFromRegistry();
	void GatherActors(TArray<FMentionCandidate>& Out) const;
	static bool MatchesToken(const FMentionCandidate& C, const FString& TokenLower);
	static void SortCandidates(TArray<FMentionCandidate>& Items);

	TArray<FMentionCandidate> AssetCandidates;
	mutable FCriticalSection AssetLock;

	bool bAssetIndexReady = false;
	bool bAssetIndexBuilding = false;

};
