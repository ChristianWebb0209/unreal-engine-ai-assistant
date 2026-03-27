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
	virtual FUnrealAiRetrievalProjectStatus GetProjectStatus(const FString& ProjectId) const override;
	virtual void RequestRebuild(const FString& ProjectId) override;

private:
	void EnsureBackgroundIndexBuild(const FString& ProjectId, const FUnrealAiRetrievalSettings& Settings);
	bool BuildOrRebuildIndexNow(const FString& ProjectId, const FUnrealAiRetrievalSettings& Settings, FString& OutError);
	void CollectIndexableFiles(const FString& ProjectDir, TArray<FString>& OutFiles) const;
	void ChunkFileText(const FString& RelativePath, const FString& Text, TArray<struct FUnrealAiVectorChunkRow>& OutChunks) const;
	void CollectBlueprintFeatureChunks(TArray<struct FUnrealAiVectorChunkRow>& OutChunks) const;
	void CollectMemoryChunks(TArray<struct FUnrealAiVectorChunkRow>& OutChunks) const;

	IUnrealAiPersistence* Persistence = nullptr;
	FUnrealAiModelProfileRegistry* Profiles = nullptr;
	IUnrealAiMemoryService* MemoryService = nullptr;
	TUniquePtr<IUnrealAiEmbeddingProvider> EmbeddingProvider;
	mutable FCriticalSection IndexStateMutex;
	TSet<FString> IndexBuildsInFlight;
};
