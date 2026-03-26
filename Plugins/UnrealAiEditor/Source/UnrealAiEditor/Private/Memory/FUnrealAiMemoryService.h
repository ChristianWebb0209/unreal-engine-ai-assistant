#pragma once

#include "CoreMinimal.h"
#include "Memory/IUnrealAiMemoryService.h"

class IUnrealAiPersistence;

class FUnrealAiMemoryService final : public IUnrealAiMemoryService
{
public:
	explicit FUnrealAiMemoryService(IUnrealAiPersistence* InPersistence);

	virtual void Load() override;
	virtual void Flush() override;

	virtual bool UpsertMemory(FUnrealAiMemoryRecord Record) override;
	virtual bool GetMemory(const FString& MemoryId, FUnrealAiMemoryRecord& Out) override;
	virtual bool DeleteMemory(const FString& MemoryId) override;

	virtual void ListMemories(TArray<FUnrealAiMemoryIndexRow>& OutRows) const override;
	virtual void ListTombstones(TArray<FUnrealAiMemoryTombstone>& OutRows) const override;
	virtual FUnrealAiMemoryGenerationStatus GetGenerationStatus() const override;
	virtual void SetGenerationStatus(const FUnrealAiMemoryGenerationStatus& InStatus) override;

	virtual void QueryMemories(const FUnrealAiMemoryQuery& Query, TArray<FUnrealAiMemoryQueryResult>& OutResults) const override;
	virtual void QueryRelevantMemories(const FUnrealAiMemoryQuery& Query, TArray<FUnrealAiMemoryQueryResult>& OutResults) const override;

	virtual int32 Prune(const int32 MaxItems, const int32 RetentionDays, const float MinConfidence) override;

private:
	void SaveIndex();
	void SaveTombstones();
	static float ScoreRow(const FUnrealAiMemoryIndexRow& Row, const FUnrealAiMemoryQuery& Query);

private:
	IUnrealAiPersistence* Persistence = nullptr;
	TArray<FUnrealAiMemoryIndexRow> IndexRows;
	TArray<FUnrealAiMemoryTombstone> Tombstones;
	FUnrealAiMemoryGenerationStatus GenerationStatus;
};
