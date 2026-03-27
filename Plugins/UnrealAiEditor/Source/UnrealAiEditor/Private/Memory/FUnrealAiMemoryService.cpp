#include "Memory/FUnrealAiMemoryService.h"

#include "Backend/IUnrealAiPersistence.h"
#include "Memory/UnrealAiMemoryJson.h"

FUnrealAiMemoryService::FUnrealAiMemoryService(IUnrealAiPersistence* InPersistence)
	: Persistence(InPersistence)
{
}

void FUnrealAiMemoryService::Load()
{
	IndexRows.Reset();
	Tombstones.Reset();
	if (!Persistence)
	{
		return;
	}

	FString Json;
	TArray<FString> Warnings;
	if (Persistence->LoadMemoryIndexJson(Json))
	{
		UnrealAiMemoryJson::JsonToIndex(Json, IndexRows, Warnings);
	}
	Json.Reset();
	Warnings.Reset();
	if (Persistence->LoadMemoryTombstonesJson(Json))
	{
		UnrealAiMemoryJson::JsonToTombstones(Json, Tombstones, Warnings);
	}
	Json.Reset();
	Warnings.Reset();
	if (Persistence->LoadMemoryGenerationStatusJson(Json))
	{
		UnrealAiMemoryJson::JsonToGenerationStatus(Json, GenerationStatus, Warnings);
	}
}

void FUnrealAiMemoryService::Flush()
{
	SaveIndex();
	SaveTombstones();
}

bool FUnrealAiMemoryService::UpsertMemory(FUnrealAiMemoryRecord Record)
{
	if (!Persistence || Record.Id.IsEmpty() || Record.Title.IsEmpty())
	{
		return false;
	}
	Record.UpdatedAtUtc = FDateTime::UtcNow();
	if (Record.CreatedAtUtc.GetTicks() <= 0)
	{
		Record.CreatedAtUtc = Record.UpdatedAtUtc;
	}

	FString Json;
	if (!UnrealAiMemoryJson::RecordToJson(Record, Json))
	{
		return false;
	}
	if (!Persistence->SaveMemoryItemJson(Record.Id, Json))
	{
		return false;
	}

	bool bFound = false;
	for (FUnrealAiMemoryIndexRow& Row : IndexRows)
	{
		if (Row.Id != Record.Id)
		{
			continue;
		}
		Row.Title = Record.Title;
		Row.Description = Record.Description;
		Row.Tags = Record.Tags;
		Row.Scope = Record.Scope;
		Row.Status = Record.Status;
		Row.Confidence = Record.Confidence;
		Row.UseCount = Record.UseCount;
		Row.UpdatedAtUtc = Record.UpdatedAtUtc;
		Row.LastUsedAtUtc = Record.LastUsedAtUtc;
		bFound = true;
		break;
	}
	if (!bFound)
	{
		FUnrealAiMemoryIndexRow NewRow;
		NewRow.Id = Record.Id;
		NewRow.Title = Record.Title;
		NewRow.Description = Record.Description;
		NewRow.Tags = Record.Tags;
		NewRow.Scope = Record.Scope;
		NewRow.Status = Record.Status;
		NewRow.Confidence = Record.Confidence;
		NewRow.UseCount = Record.UseCount;
		NewRow.UpdatedAtUtc = Record.UpdatedAtUtc;
		NewRow.LastUsedAtUtc = Record.LastUsedAtUtc;
		IndexRows.Add(MoveTemp(NewRow));
	}
	SaveIndex();
	return true;
}

bool FUnrealAiMemoryService::GetMemory(const FString& MemoryId, FUnrealAiMemoryRecord& Out)
{
	if (!Persistence || MemoryId.IsEmpty())
	{
		return false;
	}
	FString Json;
	if (!Persistence->LoadMemoryItemJson(MemoryId, Json))
	{
		return false;
	}
	TArray<FString> Warnings;
	return UnrealAiMemoryJson::JsonToRecord(Json, Out, Warnings);
}

bool FUnrealAiMemoryService::DeleteMemory(const FString& MemoryId)
{
	if (!Persistence || MemoryId.IsEmpty())
	{
		return false;
	}
	Persistence->DeleteMemoryItemJson(MemoryId);
	IndexRows.RemoveAll([&MemoryId](const FUnrealAiMemoryIndexRow& Row)
	{
		return Row.Id == MemoryId;
	});
	{
		FUnrealAiMemoryTombstone T;
		T.Id = MemoryId;
		T.DeletedAtUtc = FDateTime::UtcNow();
		Tombstones.Add(MoveTemp(T));
	}
	SaveIndex();
	SaveTombstones();
	return true;
}

void FUnrealAiMemoryService::ListMemories(TArray<FUnrealAiMemoryIndexRow>& OutRows) const
{
	OutRows = IndexRows;
	OutRows.Sort([](const FUnrealAiMemoryIndexRow& A, const FUnrealAiMemoryIndexRow& B)
	{
		return A.UpdatedAtUtc > B.UpdatedAtUtc;
	});
}

void FUnrealAiMemoryService::ListTombstones(TArray<FUnrealAiMemoryTombstone>& OutRows) const
{
	OutRows = Tombstones;
	OutRows.Sort([](const FUnrealAiMemoryTombstone& A, const FUnrealAiMemoryTombstone& B)
	{
		return A.DeletedAtUtc > B.DeletedAtUtc;
	});
}

FUnrealAiMemoryGenerationStatus FUnrealAiMemoryService::GetGenerationStatus() const
{
	return GenerationStatus;
}

void FUnrealAiMemoryService::SetGenerationStatus(const FUnrealAiMemoryGenerationStatus& InStatus)
{
	GenerationStatus = InStatus;
	if (!Persistence)
	{
		return;
	}
	FString Json;
	if (UnrealAiMemoryJson::GenerationStatusToJson(GenerationStatus, Json))
	{
		Persistence->SaveMemoryGenerationStatusJson(Json);
	}
}

float FUnrealAiMemoryService::ScoreRow(const FUnrealAiMemoryIndexRow& Row, const FUnrealAiMemoryQuery& Query)
{
	float Score = 0.0f;
	const FString QueryLower = Query.QueryText.ToLower();
	if (!QueryLower.IsEmpty())
	{
		if (Row.Title.ToLower().Contains(QueryLower))
		{
			Score += 5.0f;
		}
		if (Row.Description.ToLower().Contains(QueryLower))
		{
			Score += 2.0f;
		}
	}
	for (const FString& Tag : Query.RequiredTags)
	{
		if (Row.Tags.Contains(Tag))
		{
			Score += 1.8f;
		}
	}
	if (Query.bPreferThreadScope && Row.Scope == EUnrealAiMemoryScope::Thread)
	{
		Score += 1.25f;
	}
	if (!Query.PreferredThreadId.IsEmpty())
	{
		const FString ThreadTag = FString::Printf(TEXT("thread_%s"), *Query.PreferredThreadId.Left(8).ToLower());
		if (Row.Tags.Contains(ThreadTag))
		{
			Score += 4.5f;
		}
	}
	Score += Row.Confidence * 3.0f;
	return Score;
}

void FUnrealAiMemoryService::QueryMemories(const FUnrealAiMemoryQuery& Query, TArray<FUnrealAiMemoryQueryResult>& OutResults) const
{
	OutResults.Reset();
	TSet<FString> DedupKeys;
	for (const FUnrealAiMemoryIndexRow& Row : IndexRows)
	{
		if (Row.Confidence < Query.MinConfidence || Row.Status != EUnrealAiMemoryStatus::Active)
		{
			continue;
		}
		bool bAllTags = true;
		for (const FString& RequiredTag : Query.RequiredTags)
		{
			if (!Row.Tags.Contains(RequiredTag))
			{
				bAllTags = false;
				break;
			}
		}
		if (!bAllTags)
		{
			continue;
		}
		const FString DedupKey = (Row.Title + TEXT("|") + Row.Description).ToLower();
		if (DedupKeys.Contains(DedupKey))
		{
			continue;
		}
		DedupKeys.Add(DedupKey);
		FUnrealAiMemoryQueryResult Hit;
		Hit.IndexRow = Row;
		Hit.Score = ScoreRow(Row, Query);
		OutResults.Add(MoveTemp(Hit));
	}
	TArray<FUnrealAiMemoryQueryResult> BaseRanked = OutResults;
	BaseRanked.Sort([](const FUnrealAiMemoryQueryResult& A, const FUnrealAiMemoryQueryResult& B)
	{
		if (!FMath::IsNearlyEqual(A.Score, B.Score))
		{
			return A.Score > B.Score;
		}
		return A.IndexRow.UpdatedAtUtc > B.IndexRow.UpdatedAtUtc;
	});
	OutResults.Reset();
	auto TagSimilarity = [](const TArray<FString>& A, const TArray<FString>& B) -> float
	{
		if (A.Num() == 0 || B.Num() == 0)
		{
			return 0.0f;
		}
		TSet<FString> SA;
		TSet<FString> SB;
		for (const FString& T : A) { SA.Add(T.ToLower()); }
		for (const FString& T : B) { SB.Add(T.ToLower()); }
		int32 Inter = 0;
		for (const FString& T : SA)
		{
			if (SB.Contains(T))
			{
				++Inter;
			}
		}
		const int32 Union = SA.Num() + SB.Num() - Inter;
		return Union > 0 ? static_cast<float>(Inter) / static_cast<float>(Union) : 0.0f;
	};
	while (BaseRanked.Num() > 0)
	{
		int32 BestIdx = 0;
		float BestAdjusted = -FLT_MAX;
		for (int32 i = 0; i < BaseRanked.Num(); ++i)
		{
			const FUnrealAiMemoryQueryResult& Candidate = BaseRanked[i];
			float MaxSim = 0.0f;
			for (const FUnrealAiMemoryQueryResult& Chosen : OutResults)
			{
				MaxSim = FMath::Max(MaxSim, TagSimilarity(Candidate.IndexRow.Tags, Chosen.IndexRow.Tags));
			}
			// Diversity penalty to avoid near-duplicate memories crowding the top of results.
			const float Adjusted = Candidate.Score - (MaxSim * 2.5f);
			if (Adjusted > BestAdjusted)
			{
				BestAdjusted = Adjusted;
				BestIdx = i;
			}
		}
		OutResults.Add(BaseRanked[BestIdx]);
		BaseRanked.RemoveAt(BestIdx, 1, EAllowShrinking::No);
	}
	if (Query.MaxResults > 0 && OutResults.Num() > Query.MaxResults)
	{
		OutResults.SetNum(Query.MaxResults, EAllowShrinking::No);
	}
	if (Query.bIncludeBodies)
	{
		for (FUnrealAiMemoryQueryResult& Hit : OutResults)
		{
			FUnrealAiMemoryRecord Full;
			if (const_cast<FUnrealAiMemoryService*>(this)->GetMemory(Hit.IndexRow.Id, Full))
			{
				Hit.Record = Full;
			}
		}
	}
}

void FUnrealAiMemoryService::QueryRelevantMemories(const FUnrealAiMemoryQuery& Query, TArray<FUnrealAiMemoryQueryResult>& OutResults) const
{
	QueryMemories(Query, OutResults);
}

int32 FUnrealAiMemoryService::Prune(const int32 MaxItems, const int32 RetentionDays, const float MinConfidence)
{
	int32 Removed = 0;
	const FDateTime Now = FDateTime::UtcNow();
	TArray<FString> ToDelete;
	for (const FUnrealAiMemoryIndexRow& Row : IndexRows)
	{
		const bool bLowConfidence = Row.Confidence < MinConfidence;
		const bool bExpired = RetentionDays > 0 && (Now - Row.UpdatedAtUtc).GetTotalDays() > static_cast<double>(RetentionDays);
		if (bLowConfidence || bExpired || Row.Status == EUnrealAiMemoryStatus::Archived)
		{
			ToDelete.Add(Row.Id);
		}
	}
	for (const FString& Id : ToDelete)
	{
		if (DeleteMemory(Id))
		{
			++Removed;
		}
	}
	if (MaxItems > 0 && IndexRows.Num() > MaxItems)
	{
		IndexRows.Sort([](const FUnrealAiMemoryIndexRow& A, const FUnrealAiMemoryIndexRow& B)
		{
			return A.UpdatedAtUtc > B.UpdatedAtUtc;
		});
		while (IndexRows.Num() > MaxItems)
		{
			const FString Victim = IndexRows.Last().Id;
			if (DeleteMemory(Victim))
			{
				++Removed;
			}
		}
	}
	return Removed;
}

void FUnrealAiMemoryService::SaveIndex()
{
	if (!Persistence)
	{
		return;
	}
	FString Json;
	if (UnrealAiMemoryJson::IndexToJson(IndexRows, Json))
	{
		Persistence->SaveMemoryIndexJson(Json);
	}
}

void FUnrealAiMemoryService::SaveTombstones()
{
	if (!Persistence)
	{
		return;
	}
	FString Json;
	if (UnrealAiMemoryJson::TombstonesToJson(Tombstones, Json))
	{
		Persistence->SaveMemoryTombstonesJson(Json);
	}
}
