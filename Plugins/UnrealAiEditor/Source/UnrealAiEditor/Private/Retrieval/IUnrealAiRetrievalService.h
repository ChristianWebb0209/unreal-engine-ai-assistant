#pragma once

#include "Retrieval/UnrealAiRetrievalTypes.h"

class IUnrealAiRetrievalService
{
public:
	virtual ~IUnrealAiRetrievalService() = default;

	virtual FUnrealAiRetrievalSettings LoadSettings() const = 0;
	virtual bool IsEnabledForProject(const FString& ProjectId) const = 0;
	virtual FUnrealAiRetrievalQueryResult Query(const FUnrealAiRetrievalQuery& Query) = 0;
	virtual void StartPrefetch(const FUnrealAiRetrievalQuery& Query, const FString& TurnKey) = 0;
	virtual bool TryConsumePrefetch(const FString& TurnKey, FUnrealAiRetrievalQueryResult& OutResult, bool& bOutReady) = 0;
	virtual void CancelPrefetchForThread(const FString& ProjectId, const FString& ThreadId) = 0;
	virtual FUnrealAiRetrievalProjectStatus GetProjectStatus(const FString& ProjectId) const = 0;
	virtual bool GetVectorDbOverview(
		const FString& ProjectId,
		FUnrealAiRetrievalVectorDbOverview& OutOverview,
		FString& OutError) const = 0;
	virtual bool GetVectorDbTopGraphData(
		const FString& ProjectId,
		int32 TopN,
		int32 SamplePerSource,
		TArray<FUnrealAiVectorDbTopSourceRow>& OutSources,
		FString& OutError) const = 0;
	virtual void RequestRebuild(const FString& ProjectId) = 0;
};
