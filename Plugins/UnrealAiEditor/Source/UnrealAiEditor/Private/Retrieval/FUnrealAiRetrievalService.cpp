#include "Retrieval/FUnrealAiRetrievalService.h"

#include "Backend/IUnrealAiPersistence.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Memory/UnrealAiMemoryTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Retrieval/FOpenAiCompatibleEmbeddingProvider.h"
#include "Retrieval/UnrealAiBlueprintFeatureExtractor.h"
#include "Retrieval/UnrealAiRetrievalDiagnostics.h"
#include "Retrieval/UnrealAiVectorIndexStore.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FUnrealAiRetrievalService::FUnrealAiRetrievalService(
	IUnrealAiPersistence* InPersistence,
	FUnrealAiModelProfileRegistry* InProfiles,
	IUnrealAiMemoryService* InMemoryService)
	: Persistence(InPersistence)
	, Profiles(InProfiles)
	, MemoryService(InMemoryService)
{
	EmbeddingProvider = MakeUnique<FOpenAiCompatibleEmbeddingProvider>(Profiles);
}

FUnrealAiRetrievalService::~FUnrealAiRetrievalService() = default;

namespace
{
	static FString MakeChunkId(const FString& SourcePath, const int32 ChunkStart, const FString& ChunkText)
	{
		return SourcePath + TEXT(":") + FString::FromInt(ChunkStart) + TEXT(":") + FMD5::HashAnsiString(*ChunkText);
	}
}

FUnrealAiRetrievalSettings FUnrealAiRetrievalService::LoadSettings() const
{
	FUnrealAiRetrievalSettings Settings;
	if (!Persistence)
	{
		return Settings;
	}

	FString Json;
	if (!Persistence->LoadSettingsJson(Json) || Json.IsEmpty())
	{
		return Settings;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return Settings;
	}

	const TSharedPtr<FJsonObject>* RetrievalObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("retrieval"), RetrievalObj) || !RetrievalObj || !(*RetrievalObj).IsValid())
	{
		return Settings;
	}

	(*RetrievalObj)->TryGetBoolField(TEXT("enabled"), Settings.bEnabled);
	(*RetrievalObj)->TryGetStringField(TEXT("embeddingModel"), Settings.EmbeddingModel);
	double NumberField = 0.0;
	if ((*RetrievalObj)->TryGetNumberField(TEXT("maxSnippetsPerTurn"), NumberField))
	{
		Settings.MaxSnippetsPerTurn = FMath::Max(0, static_cast<int32>(NumberField));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("maxSnippetTokens"), NumberField))
	{
		Settings.MaxSnippetTokens = FMath::Max(0, static_cast<int32>(NumberField));
	}
	(*RetrievalObj)->TryGetBoolField(TEXT("autoIndexOnProjectOpen"), Settings.bAutoIndexOnProjectOpen);
	if ((*RetrievalObj)->TryGetNumberField(TEXT("periodicScrubMinutes"), NumberField))
	{
		Settings.PeriodicScrubMinutes = FMath::Max(0, static_cast<int32>(NumberField));
	}
	(*RetrievalObj)->TryGetBoolField(TEXT("allowMixedModelCompatibility"), Settings.bAllowMixedModelCompatibility);

	return Settings;
}

bool FUnrealAiRetrievalService::IsEnabledForProject(const FString& ProjectId) const
{
	if (ProjectId.IsEmpty())
	{
		return false;
	}
	return LoadSettings().bEnabled;
}

FUnrealAiRetrievalProjectStatus FUnrealAiRetrievalService::GetProjectStatus(const FString& ProjectId) const
{
	FUnrealAiRetrievalProjectStatus Status;
	const FUnrealAiRetrievalSettings Settings = LoadSettings();
	Status.bEnabled = Settings.bEnabled;
	if (!Settings.bEnabled)
	{
		Status.StateText = TEXT("disabled");
		return Status;
	}
	if (ProjectId.IsEmpty())
	{
		Status.StateText = TEXT("no_project");
		return Status;
	}
	FUnrealAiVectorIndexStore Store(ProjectId);
	FUnrealAiVectorManifest Manifest;
	if (!Store.LoadManifest(Manifest))
	{
		Status.StateText = TEXT("indexing");
		return Status;
	}
	{
		FScopeLock Lock(&IndexStateMutex);
		Status.bBusy = IndexBuildsInFlight.Contains(ProjectId);
	}
	if (!Status.bBusy && Manifest.Status.Equals(TEXT("indexing"), ESearchCase::IgnoreCase))
	{
		int32 DbFiles = 0;
		int32 DbChunks = 0;
		FString CountError;
		if (Store.GetIndexCounts(DbFiles, DbChunks, CountError) && DbChunks > 0)
		{
			Manifest.FilesIndexed = DbFiles;
			Manifest.ChunksIndexed = DbChunks;
			Manifest.Status = TEXT("ready");
			Store.SaveManifest(Manifest);
			TArray<TPair<FString, int32>> Top;
			FString TopErr;
			Store.GetTopSourcesByChunkCount(12, Top, TopErr);
			TArray<FUnrealAiRetrievalDiagnosticsRow> Rows;
			for (const TPair<FString, int32>& P : Top)
			{
				FUnrealAiRetrievalDiagnosticsRow R;
				R.SourceId = P.Key;
				R.ChunkCount = P.Value;
				Rows.Add(MoveTemp(R));
			}
			UnrealAiRetrievalDiagnostics::WriteIndexDiagnostics(ProjectId, TEXT("ready"), Manifest.FilesIndexed, Manifest.ChunksIndexed, Rows);
		}
	}
	Status.StateText = Manifest.Status;
	Status.FilesIndexed = Manifest.FilesIndexed;
	Status.ChunksIndexed = Manifest.ChunksIndexed;
	if (Status.StateText.Equals(TEXT("ready"), ESearchCase::IgnoreCase) && Status.FilesIndexed > 0 && Status.ChunksIndexed > 0)
	{
		TArray<TPair<FString, int32>> Top;
		FString TopErr;
		Store.GetTopSourcesByChunkCount(12, Top, TopErr);
		TArray<FUnrealAiRetrievalDiagnosticsRow> Rows;
		for (const TPair<FString, int32>& P : Top)
		{
			FUnrealAiRetrievalDiagnosticsRow R;
			R.SourceId = P.Key;
			R.ChunkCount = P.Value;
			Rows.Add(MoveTemp(R));
		}
		UnrealAiRetrievalDiagnostics::WriteIndexDiagnostics(ProjectId, TEXT("ready"), Status.FilesIndexed, Status.ChunksIndexed, Rows);
	}
	return Status;
}

void FUnrealAiRetrievalService::RequestRebuild(const FString& ProjectId)
{
	if (ProjectId.IsEmpty())
	{
		return;
	}
	EnsureBackgroundIndexBuild(ProjectId, LoadSettings());
}

FUnrealAiRetrievalQueryResult FUnrealAiRetrievalService::Query(const FUnrealAiRetrievalQuery& Query)
{
	FUnrealAiRetrievalQueryResult Result;
	const FUnrealAiRetrievalSettings Settings = LoadSettings();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("Retrieval query start project_id=%s enabled=%d query_chars=%d maxResults=%d cfgMax=%d"),
		*Query.ProjectId,
		Settings.bEnabled ? 1 : 0,
		Query.QueryText.Len(),
		Query.MaxResults,
		Settings.MaxSnippetsPerTurn);
	if (!Settings.bEnabled)
	{
		return Result;
	}
	if (Query.ProjectId.IsEmpty())
	{
		Result.Warnings.Add(TEXT("Retrieval query skipped: empty project id."));
		return Result;
	}
	if (Query.QueryText.TrimStartAndEnd().IsEmpty())
	{
		Result.Warnings.Add(TEXT("Retrieval query skipped: empty query text."));
		return Result;
	}
	FUnrealAiVectorIndexStore Store(Query.ProjectId);
	FString StoreError;
	if (!Store.Initialize(StoreError))
	{
		Result.Warnings.Add(FString::Printf(TEXT("Retrieval store unavailable: %s"), *StoreError));
		return Result;
	}
	if (!Store.CheckIntegrity(StoreError))
	{
		FUnrealAiVectorManifest CorruptManifest;
		if (!Store.LoadManifest(CorruptManifest))
		{
			CorruptManifest.ProjectId = Query.ProjectId;
		}
		CorruptManifest.Status = TEXT("error");
		Store.SaveManifest(CorruptManifest);
		Result.Warnings.Add(FString::Printf(TEXT("Retrieval integrity check failed; falling back to deterministic context: %s"), *StoreError));
		EnsureBackgroundIndexBuild(Query.ProjectId, Settings);
		return Result;
	}
	FUnrealAiVectorManifest Manifest;
	const bool bHasManifest = Store.LoadManifest(Manifest);
	bool bBusy = false;
	{
		FScopeLock Lock(&IndexStateMutex);
		bBusy = IndexBuildsInFlight.Contains(Query.ProjectId);
	}
	if (bHasManifest && !bBusy && Manifest.Status.Equals(TEXT("indexing"), ESearchCase::IgnoreCase))
	{
		int32 DbFiles = 0;
		int32 DbChunks = 0;
		FString CountError;
		if (Store.GetIndexCounts(DbFiles, DbChunks, CountError) && DbChunks > 0)
		{
			Manifest.FilesIndexed = DbFiles;
			Manifest.ChunksIndexed = DbChunks;
			Manifest.Status = TEXT("ready");
			Store.SaveManifest(Manifest);
			TArray<TPair<FString, int32>> Top;
			FString TopErr;
			Store.GetTopSourcesByChunkCount(12, Top, TopErr);
			TArray<FUnrealAiRetrievalDiagnosticsRow> Rows;
			for (const TPair<FString, int32>& P : Top)
			{
				FUnrealAiRetrievalDiagnosticsRow R;
				R.SourceId = P.Key;
				R.ChunkCount = P.Value;
				Rows.Add(MoveTemp(R));
			}
			UnrealAiRetrievalDiagnostics::WriteIndexDiagnostics(Query.ProjectId, TEXT("ready"), Manifest.FilesIndexed, Manifest.ChunksIndexed, Rows);
		}
	}
	bool bCanQueryFromStore = (bHasManifest && Manifest.Status.Equals(TEXT("ready"), ESearchCase::IgnoreCase));
	if (!bCanQueryFromStore)
	{
		int32 DbFiles = 0;
		int32 DbChunks = 0;
		FString CountError;
		const bool bHasDbContent = Store.GetIndexCounts(DbFiles, DbChunks, CountError) && DbChunks > 0;
		if (bHasDbContent)
		{
			Manifest.ProjectId = Query.ProjectId;
			Manifest.FilesIndexed = DbFiles;
			Manifest.ChunksIndexed = DbChunks;
			Manifest.Status = TEXT("ready");
			Store.SaveManifest(Manifest);
			bCanQueryFromStore = true;
		}
	}
	if (!bCanQueryFromStore)
	{
		Result.Warnings.Add(TEXT("Retrieval index is not ready yet; using deterministic context."));
		if (Settings.bAutoIndexOnProjectOpen && (!bHasManifest || !Manifest.Status.Equals(TEXT("indexing"), ESearchCase::IgnoreCase)))
		{
			EnsureBackgroundIndexBuild(Query.ProjectId, Settings);
		}
		return Result;
	}
	if (Settings.PeriodicScrubMinutes > 0 && Manifest.LastIncrementalScanUtc.GetTicks() > 0)
	{
		const double AgeMinutes = (FDateTime::UtcNow() - Manifest.LastIncrementalScanUtc).GetTotalMinutes();
		if (AgeMinutes >= static_cast<double>(Settings.PeriodicScrubMinutes))
		{
			EnsureBackgroundIndexBuild(Query.ProjectId, Settings);
		}
	}
	if (!Manifest.EmbeddingModel.IsEmpty() && Manifest.EmbeddingModel != Settings.EmbeddingModel)
	{
		Manifest.MigrationState = TEXT("pending_reembed");
		Store.SaveManifest(Manifest);
		EnsureBackgroundIndexBuild(Query.ProjectId, Settings);
		if (!Settings.bAllowMixedModelCompatibility)
		{
			Result.Warnings.Add(TEXT("Retrieval model mismatch; fail-closed to deterministic context until re-embed completes."));
			return Result;
		}
	}
	if (!EmbeddingProvider)
	{
		Result.Warnings.Add(TEXT("Retrieval embedding provider unavailable."));
		return Result;
	}

	FUnrealAiEmbeddingRequest EmbeddingRequest;
	EmbeddingRequest.InputText = Query.QueryText;
	FUnrealAiEmbeddingResponse EmbeddingResponse;
	const int32 TopKRaw = Query.MaxResults > 0 ? Query.MaxResults : Settings.MaxSnippetsPerTurn;
	const int32 TopK = FMath::Max(1, TopKRaw);
	if (TopKRaw <= 0)
	{
		Result.Warnings.Add(TEXT("Retrieval max snippets <= 0; clamped to 1."));
	}
	if (!EmbeddingProvider->EmbedOne(Settings.EmbeddingModel, EmbeddingRequest, EmbeddingResponse))
	{
		Result.Warnings.Add(FString::Printf(TEXT("Retrieval query embedding failed; using lexical fallback: %s"), *EmbeddingResponse.Error));
		if (!Store.QueryTopKByLexical(Query.QueryText, TopK, Result.Snippets, StoreError))
		{
			Result.Warnings.Add(FString::Printf(TEXT("Retrieval lexical fallback failed: %s"), *StoreError));
		}
		UE_LOG(
			LogTemp,
			Log,
			TEXT("Retrieval lexical fallback project_id=%s topk=%d hits=%d"),
			*Query.ProjectId,
			TopK,
			Result.Snippets.Num());
		return Result;
	}

	if (!Store.QueryTopKByCosine(EmbeddingResponse.Vector, TopK, Result.Snippets, StoreError))
	{
		Result.Warnings.Add(FString::Printf(TEXT("Retrieval query failed: %s"), *StoreError));
	}
	else
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("Retrieval query served from vector DB project_id=%s topk=%d hits=%d"),
			*Query.ProjectId,
			TopK,
			Result.Snippets.Num());
	}
	if (Result.Snippets.Num() == 0)
	{
		// Fallback keeps retrieval useful when embeddings return no close vectors.
		if (Store.QueryTopKByLexical(Query.QueryText, TopK, Result.Snippets, StoreError))
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("Retrieval lexical fallback after empty vector hits project_id=%s topk=%d hits=%d"),
				*Query.ProjectId,
				TopK,
				Result.Snippets.Num());
		}
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("Retrieval lexical fallback failed: %s"), *StoreError));
		}
	}
	return Result;
}

void FUnrealAiRetrievalService::EnsureBackgroundIndexBuild(const FString& ProjectId, const FUnrealAiRetrievalSettings& Settings)
{
	if (ProjectId.IsEmpty())
	{
		return;
	}
	{
		FScopeLock Lock(&IndexStateMutex);
		if (IndexBuildsInFlight.Contains(ProjectId))
		{
			return;
		}
		IndexBuildsInFlight.Add(ProjectId);
	}
	Async(EAsyncExecution::ThreadPool, [this, ProjectId, Settings]()
	{
		FString Error;
		const bool bOk = BuildOrRebuildIndexNow(ProjectId, Settings, Error);
		if (!bOk)
		{
			UE_LOG(LogTemp, Warning, TEXT("Retrieval index build failed project_id=%s err=%s"), *ProjectId, *Error);
			FUnrealAiVectorIndexStore Store(ProjectId);
			FUnrealAiVectorManifest Manifest;
			if (!Store.LoadManifest(Manifest))
			{
				Manifest.ProjectId = ProjectId;
			}
			Manifest.Status = TEXT("error");
			Store.SaveManifest(Manifest);
			TArray<FUnrealAiRetrievalDiagnosticsRow> Empty;
			UnrealAiRetrievalDiagnostics::WriteIndexDiagnostics(
				ProjectId,
				TEXT("error"),
				Manifest.FilesIndexed,
				Manifest.ChunksIndexed,
				Empty);
		}
		FScopeLock Lock(&IndexStateMutex);
		IndexBuildsInFlight.Remove(ProjectId);
	});
}

void FUnrealAiRetrievalService::CollectIndexableFiles(const FString& ProjectDir, TArray<FString>& OutFiles) const
{
	OutFiles.Reset();
	TArray<FString> Candidates;
	IFileManager::Get().FindFilesRecursive(Candidates, *FPaths::Combine(ProjectDir, TEXT("Source")), TEXT("*.*"), true, false);
	IFileManager::Get().FindFilesRecursive(Candidates, *FPaths::Combine(ProjectDir, TEXT("docs")), TEXT("*.*"), true, false);
	for (const FString& Path : Candidates)
	{
		const FString Ext = FPaths::GetExtension(Path, true).ToLower();
		if (Ext == TEXT(".h") || Ext == TEXT(".hpp") || Ext == TEXT(".cpp") || Ext == TEXT(".md") || Ext == TEXT(".txt") || Ext == TEXT(".ini"))
		{
			OutFiles.Add(Path);
		}
	}
}

void FUnrealAiRetrievalService::ChunkFileText(const FString& RelativePath, const FString& Text, TArray<FUnrealAiVectorChunkRow>& OutChunks) const
{
	const int32 ChunkChars = 1200;
	const int32 OverlapChars = 200;
	if (Text.IsEmpty())
	{
		return;
	}
	int32 Start = 0;
	while (Start < Text.Len())
	{
		const int32 Len = FMath::Min(ChunkChars, Text.Len() - Start);
		const FString ChunkText = Text.Mid(Start, Len);
		FUnrealAiVectorChunkRow Row;
		Row.SourcePath = RelativePath;
		Row.Text = ChunkText;
		Row.ContentHash = FMD5::HashAnsiString(*ChunkText);
		Row.ChunkId = MakeChunkId(RelativePath, Start, ChunkText);
		OutChunks.Add(MoveTemp(Row));
		if (Start + Len >= Text.Len())
		{
			break;
		}
		Start += (ChunkChars - OverlapChars);
	}
}

void FUnrealAiRetrievalService::CollectBlueprintFeatureChunks(TArray<FUnrealAiVectorChunkRow>& OutChunks) const
{
	TArray<FUnrealAiBlueprintFeatureRecord> Records;
	if (IsInGameThread())
	{
		FUnrealAiBlueprintFeatureExtractor::ExtractFeatureRecords(Records);
	}
	else
	{
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(false);
		AsyncTask(ENamedThreads::GameThread, [&Records, Done]()
		{
			FUnrealAiBlueprintFeatureExtractor::ExtractFeatureRecords(Records);
			if (Done)
			{
				Done->Trigger();
			}
		});
		if (Done)
		{
			Done->Wait();
			FPlatformProcess::ReturnSynchEventToPool(Done);
		}
	}
	const int32 MaxCharsPerRecord = 1200;
	for (const FUnrealAiBlueprintFeatureRecord& Record : Records)
	{
		FString Text = Record.Text;
		if (Text.Len() > MaxCharsPerRecord)
		{
			Text = Text.Left(MaxCharsPerRecord);
		}
		FUnrealAiVectorChunkRow Row;
		Row.SourcePath = Record.AssetPath;
		Row.Text = Text;
		Row.ContentHash = FMD5::HashAnsiString(*Text);
		Row.ChunkId = MakeChunkId(Record.AssetPath, 0, Text);
		OutChunks.Add(MoveTemp(Row));
	}
}

void FUnrealAiRetrievalService::CollectMemoryChunks(TArray<FUnrealAiVectorChunkRow>& OutChunks) const
{
	if (!MemoryService)
	{
		return;
	}
	TArray<FUnrealAiMemoryIndexRow> Rows;
	MemoryService->ListMemories(Rows);
	const int32 MaxMemoryRecords = 2000;
	int32 Added = 0;
	for (const FUnrealAiMemoryIndexRow& Row : Rows)
	{
		if (Added >= MaxMemoryRecords)
		{
			break;
		}
		FUnrealAiMemoryRecord Full;
		if (!MemoryService->GetMemory(Row.Id, Full))
		{
			continue;
		}
		FString ThreadToken = TEXT("project");
		for (const FString& Tag : Row.Tags)
		{
			if (Tag.StartsWith(TEXT("thread_")))
			{
				ThreadToken = Tag.RightChop(7);
				break;
			}
		}
		const FString Text = FString::Printf(TEXT("memory id=%s title=%s description=%s body=%s"),
			*Row.Id,
			*Row.Title,
			*Row.Description,
			*Full.Body.Left(2000));
		FUnrealAiVectorChunkRow Chunk;
		Chunk.SourcePath = FString::Printf(TEXT("memory:%s:thread:%s"), *Row.Id, *ThreadToken);
		Chunk.Text = Text;
		Chunk.ContentHash = FMD5::HashAnsiString(*Text);
		Chunk.ChunkId = MakeChunkId(Chunk.SourcePath, 0, Text);
		OutChunks.Add(MoveTemp(Chunk));
		++Added;
	}
}

bool FUnrealAiRetrievalService::BuildOrRebuildIndexNow(const FString& ProjectId, const FUnrealAiRetrievalSettings& Settings, FString& OutError)
{
	FUnrealAiVectorIndexStore Store(ProjectId);
	if (!Store.Initialize(OutError))
	{
		return false;
	}
	FUnrealAiVectorManifest Manifest;
	const bool bHadManifest = Store.LoadManifest(Manifest);
	Manifest.ProjectId = ProjectId;
	Manifest.EmbeddingModel = Settings.EmbeddingModel;
	{
		// Keep manifest counters aligned to DB, even while a rebuild is in progress.
		int32 CurrentFiles = 0;
		int32 CurrentChunks = 0;
		FString CountError;
		if (Store.GetIndexCounts(CurrentFiles, CurrentChunks, CountError))
		{
			Manifest.FilesIndexed = CurrentFiles;
			Manifest.ChunksIndexed = CurrentChunks;
		}
	}
	// Preserve terminal error state until a successful commit transitions back to ready.
	if (!(bHadManifest && Manifest.Status.Equals(TEXT("error"), ESearchCase::IgnoreCase)))
	{
		Manifest.Status = TEXT("indexing");
	}
	Store.SaveManifest(Manifest);

	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	TArray<FString> Files;
	CollectIndexableFiles(ProjectDir, Files);

	TMap<FString, FString> ExistingSourceHashes;
	Store.LoadSourceHashes(ExistingSourceHashes, OutError);
	TMap<FString, FString> NewSourceHashes;
	TMap<FString, TArray<FUnrealAiVectorChunkRow>> ChangedChunksBySource;
	TArray<FString> RemovedSources;
	ExistingSourceHashes.GetKeys(RemovedSources);
	TArray<FUnrealAiVectorChunkRow> AllChunks;
	int32 ChangedFiles = 0;
	for (const FString& AbsolutePath : Files)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *AbsolutePath))
		{
			continue;
		}
		const FString RelPath = AbsolutePath.Replace(*ProjectDir, TEXT(""));
		const FString SourceHash = FMD5::HashAnsiString(*Text);
		NewSourceHashes.Add(RelPath, SourceHash);
		RemovedSources.Remove(RelPath);
		const FString* ExistingHash = ExistingSourceHashes.Find(RelPath);
		const bool bChanged = !ExistingHash || *ExistingHash != SourceHash;
		if (bChanged)
		{
			++ChangedFiles;
			TArray<FUnrealAiVectorChunkRow> ChunksForSource;
			ChunkFileText(RelPath, Text, ChunksForSource);
			ChangedChunksBySource.Add(RelPath, MoveTemp(ChunksForSource));
		}
	}
	{
		TArray<FUnrealAiVectorChunkRow> BlueprintChunks;
		CollectBlueprintFeatureChunks(BlueprintChunks);
		for (FUnrealAiVectorChunkRow& Row : BlueprintChunks)
		{
			const FString SourceHash = Row.ContentHash;
			NewSourceHashes.Add(Row.SourcePath, SourceHash);
			RemovedSources.Remove(Row.SourcePath);
			const FString* ExistingHash = ExistingSourceHashes.Find(Row.SourcePath);
			const bool bChanged = !ExistingHash || *ExistingHash != SourceHash;
			if (bChanged)
			{
				++ChangedFiles;
				TArray<FUnrealAiVectorChunkRow>& Arr = ChangedChunksBySource.FindOrAdd(Row.SourcePath);
				Arr.Add(MoveTemp(Row));
			}
		}
	}
	{
		TArray<FUnrealAiVectorChunkRow> MemoryChunks;
		CollectMemoryChunks(MemoryChunks);
		for (FUnrealAiVectorChunkRow& Row : MemoryChunks)
		{
			const FString SourceHash = Row.ContentHash;
			NewSourceHashes.Add(Row.SourcePath, SourceHash);
			RemovedSources.Remove(Row.SourcePath);
			const FString* ExistingHash = ExistingSourceHashes.Find(Row.SourcePath);
			const bool bChanged = !ExistingHash || *ExistingHash != SourceHash;
			if (bChanged)
			{
				++ChangedFiles;
				TArray<FUnrealAiVectorChunkRow>& Arr = ChangedChunksBySource.FindOrAdd(Row.SourcePath);
				Arr.Add(MoveTemp(Row));
			}
		}
	}

	for (TPair<FString, TArray<FUnrealAiVectorChunkRow>>& Pair : ChangedChunksBySource)
	{
		for (FUnrealAiVectorChunkRow& Chunk : Pair.Value)
		{
			FUnrealAiEmbeddingRequest Req;
			Req.InputText = Chunk.Text.Left(6000);
			FUnrealAiEmbeddingResponse Resp;
			if (!EmbeddingProvider.IsValid() || !EmbeddingProvider->EmbedOne(Settings.EmbeddingModel, Req, Resp))
			{
				OutError = Resp.Error.IsEmpty() ? TEXT("Embedding generation failed during indexing.") : Resp.Error;
				Manifest.Status = TEXT("error");
				Store.SaveManifest(Manifest);
				return false;
			}
			Chunk.Embedding = MoveTemp(Resp.Vector);
			AllChunks.Add(Chunk);
		}
	}

	if (ExistingSourceHashes.Num() == 0)
	{
		TArray<FUnrealAiVectorChunkRow> InitialChunks;
		for (TPair<FString, TArray<FUnrealAiVectorChunkRow>>& Pair : ChangedChunksBySource)
		{
			for (FUnrealAiVectorChunkRow& Chunk : Pair.Value)
			{
				InitialChunks.Add(Chunk);
			}
		}
		if (!Store.UpsertAllChunks(InitialChunks, OutError))
		{
			Manifest.Status = TEXT("error");
			Store.SaveManifest(Manifest);
			return false;
		}
	}
	else if (!Store.UpsertIncremental(NewSourceHashes, ChangedChunksBySource, RemovedSources, OutError))
	{
		Manifest.Status = TEXT("error");
		Store.SaveManifest(Manifest);
		return false;
	}
	int32 DbFiles = 0;
	int32 DbChunks = 0;
	Store.GetIndexCounts(DbFiles, DbChunks, OutError);
	Manifest.FilesIndexed = DbFiles;
	Manifest.ChunksIndexed = DbChunks;
	Manifest.LastFullScanUtc = FDateTime::UtcNow();
	Manifest.LastIncrementalScanUtc = Manifest.LastFullScanUtc;
	Manifest.MigrationState = TEXT("none");
	Manifest.Status = ChangedFiles > 0 || RemovedSources.Num() > 0 ? TEXT("ready") : TEXT("ready");
	Store.SaveManifest(Manifest);

	{
		TArray<TPair<FString, int32>> Top;
		FString Err;
		Store.GetTopSourcesByChunkCount(12, Top, Err);
		TArray<FUnrealAiRetrievalDiagnosticsRow> Rows;
		for (const TPair<FString, int32>& P : Top)
		{
			FUnrealAiRetrievalDiagnosticsRow R;
			R.SourceId = P.Key;
			R.ChunkCount = P.Value;
			Rows.Add(MoveTemp(R));
		}
		UnrealAiRetrievalDiagnostics::WriteIndexDiagnostics(ProjectId, Manifest.Status, Manifest.FilesIndexed, Manifest.ChunksIndexed, Rows);
	}
	return true;
}
