#pragma once

#include "CoreMinimal.h"
#include "Memory/UnrealAiMemoryTypes.h"

class IUnrealAiMemoryService
{
public:
	virtual ~IUnrealAiMemoryService() = default;

	virtual void Load() = 0;
	virtual void Flush() = 0;

	virtual bool UpsertMemory(FUnrealAiMemoryRecord Record) = 0;
	virtual bool GetMemory(const FString& MemoryId, FUnrealAiMemoryRecord& Out) = 0;
	virtual bool DeleteMemory(const FString& MemoryId) = 0;

	virtual void ListMemories(TArray<FUnrealAiMemoryIndexRow>& OutRows) const = 0;
	virtual void ListTombstones(TArray<FUnrealAiMemoryTombstone>& OutRows) const = 0;
	virtual FUnrealAiMemoryGenerationStatus GetGenerationStatus() const = 0;
	virtual void SetGenerationStatus(const FUnrealAiMemoryGenerationStatus& InStatus) = 0;

	virtual void QueryMemories(const FUnrealAiMemoryQuery& Query, TArray<FUnrealAiMemoryQueryResult>& OutResults) const = 0;

	/** Future seam for context manager staged ingestion (deferred wiring). */
	virtual void QueryRelevantMemories(const FUnrealAiMemoryQuery& Query, TArray<FUnrealAiMemoryQueryResult>& OutResults) const = 0;

	virtual int32 Prune(const int32 MaxItems, const int32 RetentionDays, const float MinConfidence) = 0;
};
