#pragma once

#include "Retrieval/UnrealAiRetrievalTypes.h"

class IUnrealAiRetrievalService
{
public:
	virtual ~IUnrealAiRetrievalService() = default;

	virtual FUnrealAiRetrievalSettings LoadSettings() const = 0;
	virtual bool IsEnabledForProject(const FString& ProjectId) const = 0;
	virtual FUnrealAiRetrievalQueryResult Query(const FUnrealAiRetrievalQuery& Query) = 0;
	virtual FUnrealAiRetrievalProjectStatus GetProjectStatus(const FString& ProjectId) const = 0;
	virtual void RequestRebuild(const FString& ProjectId) = 0;
};
