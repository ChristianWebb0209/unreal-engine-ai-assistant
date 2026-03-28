#pragma once

#include "Retrieval/IUnrealAiRetrievalService.h"

class IUnrealAiPersistence;
class FUnrealAiModelProfileRegistry;
class IUnrealAiEmbeddingProvider;
class FUnrealAiVectorIndexStore;
class IUnrealAiMemoryService;

class FUnrealAiRetrievalService final : public IUnrealAiRetrievalService
{
public:
	FUnrealAiRetrievalService(IUnrealAiPersistence* InPersistence, FUnrealAiModelProfileRegistry* InProfiles, IUnrealAiMemoryService* InMemoryService);
	virtual ~FUnrealAiRetrievalService() override;

	virtual FUnrealAiRetrievalSettings LoadSettings() const override;
	virtual bool IsEnabledForProject(const FString& ProjectId) const override;
	virtual FUnrealAiRetrievalQueryResult Query(const FUnrealAiRetrievalQuery& Query) override;
	virtual void StartPrefetch(const FUnrealAiRetrievalQuery& Query, const FString& TurnKey) override;
	virtual bool TryConsumePrefetch(const FString& TurnKey, FUnrealAiRetrievalQueryResult& OutResult, bool& bOutReady) override;
	virtual void CancelPrefetchForThread(const FString& ProjectId, const FString& ThreadId) override;
	virtual FUnrealAiRetrievalProjectStatus GetProjectStatus(const FString& ProjectId) const override;
	virtual bool GetVectorDbOverview(
		const FString& ProjectId,
		FUnrealAiRetrievalVectorDbOverview& OutOverview,
		FString& OutError) const override;
	virtual void RequestRebuild(const FString& ProjectId) override;

private:
	FUnrealAiVectorIndexStore* GetOrCreateStore(const FString& ProjectId, FString& OutError) const;
	void EnsureBackgroundIndexBuild(const FString& ProjectId, const FUnrealAiRetrievalSettings& Settings);
	bool BuildOrRebuildIndexNow(const FString& ProjectId, const FUnrealAiRetrievalSettings& Settings, FString& OutError);
	void ChunkFileText(
		const FString& RelativePath,
		const FString& Text,
		int32 ChunkChars,
		int32 ChunkOverlap,
		TArray<struct FUnrealAiVectorChunkRow>& OutChunks) const;
	void CollectBlueprintFeatureChunks(const FUnrealAiRetrievalSettings& Settings, TArray<struct FUnrealAiVectorChunkRow>& OutChunks) const;
	void GatherAssetRegistryShardTexts(const FUnrealAiRetrievalSettings& Settings, TArray<TPair<FString, FString>>& OutVirtualPathAndFullText) const;
	void CollectMemoryChunks(const FUnrealAiRetrievalSettings& Settings, TArray<struct FUnrealAiVectorChunkRow>& OutChunks) const;

	IUnrealAiPersistence* Persistence = nullptr;
	FUnrealAiModelProfileRegistry* Profiles = nullptr;
	IUnrealAiMemoryService* MemoryService = nullptr;
	TUniquePtr<IUnrealAiEmbeddingProvider> EmbeddingProvider;
	mutable FCriticalSection IndexStateMutex;
	TSet<FString> IndexBuildsInFlight;
	mutable FCriticalSection StoreCacheMutex;
	mutable TMap<FString, TUniquePtr<FUnrealAiVectorIndexStore>> CachedStoresByProject;

	// If embeddings fail (often due to HTTP timeouts), trying again on the next LLM round
	// can waste most of the harness budget repeating the same failing call.
	// This circuit breaker disables embeddings temporarily for the given ThreadId.
	mutable FCriticalSection EmbeddingCircuitMutex;
	mutable TMap<FString, double> EmbeddingFailureUntilSecondsByThread;

	struct FRetrievalQueryCacheEntry
	{
		FUnrealAiRetrievalQueryResult Result;
		double ExpiresAtSeconds = 0.0;
	};
	mutable FCriticalSection QueryCacheMutex;
	mutable TMap<FString, FRetrievalQueryCacheEntry> QueryCacheByKey;

	struct FRetrievalPrefetchEntry
	{
		FString ProjectId;
		FString ThreadId;
		uint32 QueryHash = 0u;
		bool bInFlight = false;
		bool bReady = false;
		double StartedAtSeconds = 0.0;
		double CompletedAtSeconds = 0.0;
		double ExpiresAtSeconds = 0.0;
		FUnrealAiRetrievalQueryResult Result;
	};
	mutable FCriticalSection PrefetchMutex;
	mutable TMap<FString, FRetrievalPrefetchEntry> PrefetchByTurnKey;
};
