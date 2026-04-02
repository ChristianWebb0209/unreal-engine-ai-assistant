#include "Retrieval/FUnrealAiRetrievalService.h"

#include "Backend/IUnrealAiPersistence.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Memory/UnrealAiMemoryTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"
#include "Retrieval/FOpenAiCompatibleEmbeddingProvider.h"
#include "Retrieval/UnrealAiBlueprintFeatureExtractor.h"
#include "Retrieval/UnrealAiRetrievalDiagnostics.h"
#include "Retrieval/UnrealAiRetrievalIndexConfig.h"
#include "Retrieval/UnrealAiVectorIndexStore.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#endif

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

FUnrealAiVectorIndexStore* FUnrealAiRetrievalService::GetOrCreateStore(const FString& ProjectId, FString& OutError) const
{
	if (ProjectId.IsEmpty())
	{
		OutError = TEXT("Empty ProjectId for retrieval store.");
		return nullptr;
	}
	FScopeLock Lock(&StoreCacheMutex);
	if (IndexBuildsInFlight.Contains(ProjectId))
	{
		OutError = TEXT("Vector index rebuild in progress.");
		return nullptr;
	}
	TUniquePtr<FUnrealAiVectorIndexStore>& StorePtr = CachedStoresByProject.FindOrAdd(ProjectId);
	if (!StorePtr.IsValid())
	{
		StorePtr = MakeUnique<FUnrealAiVectorIndexStore>(ProjectId);
	}
	if (!StorePtr->Initialize(OutError))
	{
		CachedStoresByProject.Remove(ProjectId);
		{
			FUnrealAiVectorIndexStore Scratch(ProjectId);
			FUnrealAiVectorManifest M;
			if (!Scratch.LoadManifest(M))
			{
				M = FUnrealAiVectorManifest();
				M.ProjectId = ProjectId;
			}
			M.Status = TEXT("error");
			M.VectorDbOpenRetryNotBeforeUtc = FDateTime::UtcNow() + FTimespan::FromMinutes(15);
			Scratch.SaveManifest(M);
		}
		return nullptr;
	}
	return StorePtr.Get();
}

namespace
{
	static EUnrealAiRetrievalRootPreset ParseRootPresetString(const FString& S)
	{
		if (S.Equals(TEXT("standard"), ESearchCase::IgnoreCase))
		{
			return EUnrealAiRetrievalRootPreset::StandardRoots;
		}
		if (S.Equals(TEXT("extended"), ESearchCase::IgnoreCase))
		{
			return EUnrealAiRetrievalRootPreset::ExtendedRoots;
		}
		return EUnrealAiRetrievalRootPreset::Minimal;
	}

	static void ClampChunkParams(int32& ChunkChars, int32& ChunkOverlap)
	{
		ChunkChars = FMath::Clamp(ChunkChars, 128, 32000);
		ChunkOverlap = FMath::Max(0, ChunkOverlap);
		if (ChunkOverlap >= ChunkChars)
		{
			ChunkOverlap = FMath::Max(0, ChunkChars - 1);
		}
	}

	static FString MakeChunkId(const FString& SourcePath, const int32 ChunkStart, const FString& ChunkText)
	{
		return SourcePath + TEXT(":") + FString::FromInt(ChunkStart) + TEXT(":") + FMD5::HashAnsiString(*ChunkText);
	}

	static uint32 MakePrefetchQueryHash(const FUnrealAiRetrievalQuery& Query)
	{
		const FString Canonical = FString::Printf(
			TEXT("%s|%s|%d|%s"),
			*Query.ProjectId,
			*Query.ThreadId,
			Query.MaxResults,
			*Query.QueryText);
		return FCrc::StrCrc32(*Canonical);
	}

	static bool IsSummarySourcePath(const FString& SourceId)
	{
		return SourceId.StartsWith(TEXT("virtual://summary/"));
	}

	static bool QueryNeedsConcretePath(const FString& QueryText)
	{
		const FString Lower = QueryText.ToLower();
		return Lower.Contains(TEXT("/game/"))
			|| Lower.Contains(TEXT("object_path"))
			|| Lower.Contains(TEXT("asset_path"))
			|| Lower.Contains(TEXT("exact path"))
			|| Lower.Contains(TEXT("concrete path"));
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

	FString PresetStr;
	if ((*RetrievalObj)->TryGetStringField(TEXT("rootPreset"), PresetStr))
	{
		Settings.RootPreset = ParseRootPresetString(PresetStr);
	}

	const TArray<TSharedPtr<FJsonValue>>* ExtArray = nullptr;
	if ((*RetrievalObj)->TryGetArrayField(TEXT("indexedExtensions"), ExtArray) && ExtArray)
	{
		Settings.IndexedExtensions.Reset();
		for (const TSharedPtr<FJsonValue>& V : *ExtArray)
		{
			if (V.IsValid() && V->Type == EJson::String)
			{
				Settings.IndexedExtensions.Add(V->AsString());
			}
		}
		UnrealAiRetrievalIndexConfig::NormalizeIndexedExtensions(Settings.IndexedExtensions);
	}

	if ((*RetrievalObj)->TryGetNumberField(TEXT("maxFilesPerRebuild"), NumberField))
	{
		Settings.MaxFilesPerRebuild = FMath::Max(0, static_cast<int32>(NumberField));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("maxTotalChunksPerRebuild"), NumberField))
	{
		Settings.MaxTotalChunksPerRebuild = FMath::Max(0, static_cast<int32>(NumberField));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("maxEmbeddingCallsPerRebuild"), NumberField))
	{
		Settings.MaxEmbeddingCallsPerRebuild = FMath::Max(0, static_cast<int32>(NumberField));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("chunkChars"), NumberField))
	{
		Settings.ChunkChars = static_cast<int32>(NumberField);
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("chunkOverlap"), NumberField))
	{
		Settings.ChunkOverlap = static_cast<int32>(NumberField);
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("assetRegistryMaxAssets"), NumberField))
	{
		Settings.AssetRegistryMaxAssets = FMath::Max(0, static_cast<int32>(NumberField));
	}
	(*RetrievalObj)->TryGetBoolField(TEXT("assetRegistryIncludeEngineAssets"), Settings.bAssetRegistryIncludeEngineAssets);
	if ((*RetrievalObj)->TryGetNumberField(TEXT("embeddingBatchSize"), NumberField))
	{
		Settings.EmbeddingBatchSize = FMath::Max(1, static_cast<int32>(NumberField));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("minDelayMsBetweenEmbeddingBatches"), NumberField))
	{
		Settings.MinDelayMsBetweenEmbeddingBatches = FMath::Max(0, static_cast<int32>(NumberField));
	}
	(*RetrievalObj)->TryGetBoolField(TEXT("indexMemoryRecordsInVectorStore"), Settings.bIndexMemoryRecordsInVectorStore);
	if ((*RetrievalObj)->TryGetNumberField(TEXT("blueprintMaxFeatureRecords"), NumberField))
	{
		Settings.BlueprintMaxFeatureRecords = FMath::Max(0, static_cast<int32>(NumberField));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("contextAggression"), NumberField))
	{
		Settings.ContextAggression = static_cast<float>(FMath::Clamp(NumberField, 0.0, 1.0));
	}

	ClampChunkParams(Settings.ChunkChars, Settings.ChunkOverlap);

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
	{
		FScopeLock Lock(&StoreCacheMutex);
		if (IndexBuildsInFlight.Contains(ProjectId))
		{
			Status.bBusy = true;
			Status.StateText = TEXT("indexing");
			return Status;
		}
	}
	FString StoreError;
	FUnrealAiVectorIndexStore* Store = GetOrCreateStore(ProjectId, StoreError);
	if (!Store)
	{
		Status.StateText = TEXT("error");
		return Status;
	}
	FUnrealAiVectorManifest Manifest;
	if (!Store->LoadManifest(Manifest))
	{
		Status.StateText = TEXT("indexing");
		return Status;
	}
	{
		FScopeLock Lock(&StoreCacheMutex);
		Status.bBusy = IndexBuildsInFlight.Contains(ProjectId);
	}
	if (!Status.bBusy && Manifest.Status.Equals(TEXT("indexing"), ESearchCase::IgnoreCase))
	{
		int32 DbFiles = 0;
		int32 DbChunks = 0;
		FString CountError;
		if (Store->GetIndexCounts(DbFiles, DbChunks, CountError) && DbChunks > 0)
		{
			Manifest.FilesIndexed = DbFiles;
			Manifest.ChunksIndexed = DbChunks;
			Manifest.Status = TEXT("ready");
			Store->SaveManifest(Manifest);
			TArray<TPair<FString, int32>> Top;
			FString TopErr;
			Store->GetTopSourcesByChunkCount(12, Top, TopErr);
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
		Store->GetTopSourcesByChunkCount(12, Top, TopErr);
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

bool FUnrealAiRetrievalService::GetVectorDbOverview(
	const FString& ProjectId,
	FUnrealAiRetrievalVectorDbOverview& OutOverview,
	FString& OutError) const
{
	OutOverview = FUnrealAiRetrievalVectorDbOverview();
	OutError.Reset();

	const FUnrealAiRetrievalSettings Settings = LoadSettings();
	OutOverview.bRetrievalEnabledInSettings = Settings.bEnabled;
	switch (Settings.RootPreset)
	{
	case EUnrealAiRetrievalRootPreset::StandardRoots:
		OutOverview.RootPresetLabel = TEXT("standard");
		break;
	case EUnrealAiRetrievalRootPreset::ExtendedRoots:
		OutOverview.RootPresetLabel = TEXT("extended");
		break;
	default:
		OutOverview.RootPresetLabel = TEXT("minimal");
		break;
	}

	TArray<FString> Exts;
	UnrealAiRetrievalIndexConfig::GetEffectiveIndexedExtensions(Settings, Exts);
	OutOverview.IndexedExtensionCount = Exts.Num();
	for (int32 i = 0; i < Exts.Num() && i < 14; ++i)
	{
		if (i > 0)
		{
			OutOverview.IndexedExtensionsPreview += TEXT(", ");
		}
		OutOverview.IndexedExtensionsPreview += Exts[i];
	}
	if (Exts.Num() > 14)
	{
		OutOverview.IndexedExtensionsPreview += FString::Printf(TEXT(" … (+%d more)"), Exts.Num() - 14);
	}

	OutOverview.bAssetRegistryCorpusEnabled = Settings.AssetRegistryMaxAssets > 0;
	OutOverview.bMemoryCorpusEnabled = Settings.bIndexMemoryRecordsInVectorStore;
	OutOverview.BlueprintMaxFeatureRecords = Settings.BlueprintMaxFeatureRecords;
	OutOverview.bBlueprintCapCustom = Settings.BlueprintMaxFeatureRecords > 0;

	if (ProjectId.IsEmpty())
	{
		OutError = TEXT("No project id.");
		return false;
	}

	const FUnrealAiRetrievalProjectStatus St = GetProjectStatus(ProjectId);
	OutOverview.bIndexBusy = St.bBusy;
	OutOverview.ManifestStatus = St.StateText;
	OutOverview.FilesIndexed = St.FilesIndexed;
	OutOverview.ChunksIndexed = St.ChunksIndexed;

	FString StoreErr;
	FUnrealAiVectorIndexStore* Store = GetOrCreateStore(ProjectId, StoreErr);
	if (!Store)
	{
		OutOverview.bStoreAvailable = false;
		OutOverview.StoreError = StoreErr;
		return true;
	}

	OutOverview.bStoreAvailable = true;
	OutOverview.IndexDbPath = Store->GetIndexDbPath();
	OutOverview.ManifestPath = Store->GetManifestPath();

	FUnrealAiVectorManifest Man;
	if (Store->LoadManifest(Man))
	{
		if (!Man.EmbeddingModel.IsEmpty())
		{
			OutOverview.ManifestEmbeddingModel = Man.EmbeddingModel;
		}
		OutOverview.MigrationState = Man.MigrationState;
		if (Man.LastFullScanUtc > FDateTime::MinValue())
		{
			OutOverview.LastFullScanUtc = Man.LastFullScanUtc;
		}
		if (!Man.Status.IsEmpty())
		{
			OutOverview.ManifestStatus = Man.Status;
		}
	}

	FString CountErr;
	int32 DbFiles = 0;
	int32 DbChunks = 0;
	if (Store->GetIndexCounts(DbFiles, DbChunks, CountErr))
	{
		OutOverview.FilesIndexed = DbFiles;
		OutOverview.ChunksIndexed = DbChunks;
	}

	FString IntErr;
	OutOverview.bIntegrityOk = Store->CheckIntegrity(IntErr);
	if (!OutOverview.bIntegrityOk)
	{
		OutOverview.IntegrityError = IntErr;
	}

	FString TopErr;
	Store->GetTopSourcesByChunkCount(8, OutOverview.TopSourcesByChunkCount, TopErr);

	return true;
}

bool FUnrealAiRetrievalService::GetVectorDbTopGraphData(
	const FString& ProjectId,
	const int32 TopN,
	const int32 SamplePerSource,
	TArray<FUnrealAiVectorDbTopSourceRow>& OutSources,
	FString& OutError) const
{
	OutSources.Reset();
	OutError.Reset();

	if (ProjectId.IsEmpty())
	{
		OutError = TEXT("No project id.");
		return false;
	}

	const int32 UseTopN = FMath::Clamp(TopN, 0, 64);
	const int32 UseSamplePerSource = FMath::Clamp(SamplePerSource, 0, 20);
	if (UseTopN <= 0 || UseSamplePerSource <= 0)
	{
		return true;
	}

	FString StoreErr;
	FUnrealAiVectorIndexStore* Store = GetOrCreateStore(ProjectId, StoreErr);
	if (!Store)
	{
		OutError = StoreErr;
		return false;
	}

	return Store->GetTopSourcesByChunkCountWithSamples(
		UseTopN,
		UseSamplePerSource,
		OutSources,
		OutError);
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
	const double QueryStartSeconds = FPlatformTime::Seconds();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("Retrieval query start project_id=%s thread_id=%s enabled=%d query_chars=%d maxResults=%d cfgMax=%d"),
		*Query.ProjectId,
		*Query.ThreadId,
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
	FString StoreError;
	FUnrealAiVectorIndexStore* Store = GetOrCreateStore(Query.ProjectId, StoreError);
	if (!Store)
	{
		Result.Warnings.Add(FString::Printf(TEXT("Retrieval store unavailable: %s"), *StoreError));
		return Result;
	}
	if (!Store->CheckIntegrity(StoreError))
	{
		FUnrealAiVectorManifest CorruptManifest;
		if (!Store->LoadManifest(CorruptManifest))
		{
			CorruptManifest.ProjectId = Query.ProjectId;
		}
		CorruptManifest.Status = TEXT("error");
		Store->SaveManifest(CorruptManifest);
		Result.Warnings.Add(FString::Printf(TEXT("Retrieval integrity check failed; falling back to deterministic context: %s"), *StoreError));
		EnsureBackgroundIndexBuild(Query.ProjectId, Settings);
		return Result;
	}
	FUnrealAiVectorManifest Manifest;
	const bool bHasManifest = Store->LoadManifest(Manifest);
	bool bBusy = false;
	{
		FScopeLock Lock(&StoreCacheMutex);
		bBusy = IndexBuildsInFlight.Contains(Query.ProjectId);
	}
	if (bHasManifest && !bBusy && Manifest.Status.Equals(TEXT("indexing"), ESearchCase::IgnoreCase))
	{
		int32 DbFiles = 0;
		int32 DbChunks = 0;
		FString CountError;
		if (Store->GetIndexCounts(DbFiles, DbChunks, CountError) && DbChunks > 0)
		{
			Manifest.FilesIndexed = DbFiles;
			Manifest.ChunksIndexed = DbChunks;
			Manifest.Status = TEXT("ready");
			Store->SaveManifest(Manifest);
			TArray<TPair<FString, int32>> Top;
			FString TopErr;
			Store->GetTopSourcesByChunkCount(12, Top, TopErr);
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
		const bool bHasDbContent = Store->GetIndexCounts(DbFiles, DbChunks, CountError) && DbChunks > 0;
		if (bHasDbContent)
		{
			Manifest.ProjectId = Query.ProjectId;
			Manifest.FilesIndexed = DbFiles;
			Manifest.ChunksIndexed = DbChunks;
			Manifest.Status = TEXT("ready");
			Store->SaveManifest(Manifest);
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
		Store->SaveManifest(Manifest);
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
	const int32 FetchK = FMath::Max(TopK, FMath::Min(TopK * 3, TopK + 24));
	if (TopKRaw <= 0)
	{
		Result.Warnings.Add(TEXT("Retrieval max snippets <= 0; clamped to 1."));
	}

	// Short-lived cache avoids repeating identical retrieval work multiple times
	// within the same harness turn/round churn.
	const FString CacheKey = FString::Printf(
		TEXT("%s|%s|%d|%s"),
		*Query.ProjectId,
		*Query.ThreadId,
		TopK,
		*Query.QueryText);
	{
		FScopeLock Lock(&QueryCacheMutex);
		if (FRetrievalQueryCacheEntry* Existing = QueryCacheByKey.Find(CacheKey))
		{
			if (Existing->ExpiresAtSeconds > QueryStartSeconds)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("Retrieval query cache hit project_id=%s thread_id=%s topk=%d hits=%d"),
					*Query.ProjectId,
					*Query.ThreadId,
					TopK,
					Existing->Result.Snippets.Num());
				return Existing->Result;
			}
			QueryCacheByKey.Remove(CacheKey);
		}
	}

	// Circuit breaker: if embeddings already timed out/faulted for this ThreadId,
	// skip embedding generation and go straight to lexical retrieval to avoid
	// repeatedly burning the HTTP timeout budget inside one harness step.
	constexpr double EmbeddingCircuitBreakerSeconds = 300.0; // 5 minutes
	double NowSeconds = FPlatformTime::Seconds();
	if (!Query.ThreadId.IsEmpty())
	{
		bool bSkipEmbeddings = false;
		{
			FScopeLock Lock(&EmbeddingCircuitMutex);
			if (const double* UntilSeconds = EmbeddingFailureUntilSecondsByThread.Find(Query.ThreadId))
			{
				if (*UntilSeconds > NowSeconds)
				{
					bSkipEmbeddings = true;
				}
				else
				{
					// Expired: allow embeddings attempts again.
					EmbeddingFailureUntilSecondsByThread.Remove(Query.ThreadId);
				}
			}
		}
		if (bSkipEmbeddings)
		{
			FString CircuitLexicalStoreError;
			if (!Store->QueryTopKByLexical(Query.QueryText, FetchK, Result.Snippets, CircuitLexicalStoreError))
			{
				Result.Warnings.Add(FString::Printf(TEXT("Retrieval lexical fallback failed (circuit breaker active): %s"), *CircuitLexicalStoreError));
			}
			ApplySummaryPreference(Query.QueryText, TopK, Result.Snippets);
			UE_LOG(
				LogTemp,
				Error,
				TEXT("Retrieval embedding circuit breaker active; skipping embeddings and using lexical only project_id=%s thread_id=%s topk=%d hits=%d"),
				*Query.ProjectId,
				*Query.ThreadId,
				TopK,
				Result.Snippets.Num());
			Result.Warnings.Add(TEXT("Retrieval embedding circuit breaker active; using lexical retrieval only."));
			{
				FScopeLock Lock(&QueryCacheMutex);
				FRetrievalQueryCacheEntry& Cached = QueryCacheByKey.FindOrAdd(CacheKey);
				Cached.Result = Result;
				Cached.ExpiresAtSeconds = QueryStartSeconds + 15.0;
			}
			return Result;
		}
	}

	if (!EmbeddingProvider->EmbedOne(Settings.EmbeddingModel, EmbeddingRequest, EmbeddingResponse))
	{
		Result.Warnings.Add(FString::Printf(TEXT("Retrieval query embedding failed; using lexical fallback: %s"), *EmbeddingResponse.Error));
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Retrieval fallback engaged (embedding failure) project_id=%s thread_id=%s err=%s"),
			*Query.ProjectId,
			*Query.ThreadId,
			*EmbeddingResponse.Error);

		// Enable circuit breaker for subsequent retrieval calls inside this harness step.
		if (!Query.ThreadId.IsEmpty())
		{
			FScopeLock Lock(&EmbeddingCircuitMutex);
			EmbeddingFailureUntilSecondsByThread.Add(Query.ThreadId, NowSeconds + EmbeddingCircuitBreakerSeconds);
		}

		if (!Store->QueryTopKByLexical(Query.QueryText, FetchK, Result.Snippets, StoreError))
		{
			Result.Warnings.Add(FString::Printf(TEXT("Retrieval lexical fallback failed: %s"), *StoreError));
		}
		ApplySummaryPreference(Query.QueryText, TopK, Result.Snippets);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("Retrieval lexical fallback project_id=%s topk=%d hits=%d"),
			*Query.ProjectId,
			TopK,
			Result.Snippets.Num());
		{
			FScopeLock Lock(&QueryCacheMutex);
			FRetrievalQueryCacheEntry& Cached = QueryCacheByKey.FindOrAdd(CacheKey);
			Cached.Result = Result;
			Cached.ExpiresAtSeconds = QueryStartSeconds + 15.0;
		}
		return Result;
	}

	if (!Store->QueryTopKByCosine(EmbeddingResponse.Vector, FetchK, Result.Snippets, StoreError))
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
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Retrieval fallback engaged (empty vector hits) project_id=%s"),
			*Query.ProjectId);
		if (Store->QueryTopKByLexical(Query.QueryText, FetchK, Result.Snippets, StoreError))
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
	ApplySummaryPreference(Query.QueryText, TopK, Result.Snippets);
	{
		FScopeLock Lock(&QueryCacheMutex);
		FRetrievalQueryCacheEntry& Cached = QueryCacheByKey.FindOrAdd(CacheKey);
		Cached.Result = Result;
		Cached.ExpiresAtSeconds = QueryStartSeconds + 15.0;
	}
	return Result;
}

void FUnrealAiRetrievalService::ApplySummaryPreference(
	const FString& QueryText,
	const int32 MaxResults,
	TArray<FUnrealAiRetrievalSnippet>& InOutSnippets) const
{
	if (InOutSnippets.Num() <= 1)
	{
		return;
	}
	const bool bNeedsConcretePath = QueryNeedsConcretePath(QueryText);
	InOutSnippets.Sort([bNeedsConcretePath](const FUnrealAiRetrievalSnippet& A, const FUnrealAiRetrievalSnippet& B)
	{
		const bool bASummary = IsSummarySourcePath(A.SourceId);
		const bool bBSummary = IsSummarySourcePath(B.SourceId);
		float AScore = A.Score + (bASummary ? 0.08f : 0.0f);
		float BScore = B.Score + (bBSummary ? 0.08f : 0.0f);
		if (!bNeedsConcretePath)
		{
			AScore += bASummary ? 0.02f : -0.02f;
			BScore += bBSummary ? 0.02f : -0.02f;
		}
		if (!FMath::IsNearlyEqual(AScore, BScore))
		{
			return AScore > BScore;
		}
		return A.SourceId < B.SourceId;
	});
	if (MaxResults > 0 && InOutSnippets.Num() > MaxResults)
	{
		InOutSnippets.SetNum(MaxResults);
	}
}

void FUnrealAiRetrievalService::StartPrefetch(const FUnrealAiRetrievalQuery& InQuery, const FString& TurnKey)
{
	if (TurnKey.IsEmpty() || InQuery.ProjectId.IsEmpty() || InQuery.QueryText.TrimStartAndEnd().IsEmpty())
	{
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	const uint32 QueryHash = MakePrefetchQueryHash(InQuery);
	{
		FScopeLock Lock(&PrefetchMutex);
		for (auto It = PrefetchByTurnKey.CreateIterator(); It; ++It)
		{
			if (It.Value().ExpiresAtSeconds <= NowSeconds)
			{
				It.RemoveCurrent();
			}
		}

		if (FRetrievalPrefetchEntry* Existing = PrefetchByTurnKey.Find(TurnKey))
		{
			if (Existing->QueryHash == QueryHash && Existing->ExpiresAtSeconds > NowSeconds)
			{
				return;
			}
		}

		FRetrievalPrefetchEntry& Entry = PrefetchByTurnKey.FindOrAdd(TurnKey);
		Entry.ProjectId = InQuery.ProjectId;
		Entry.ThreadId = InQuery.ThreadId;
		Entry.QueryHash = QueryHash;
		Entry.bInFlight = true;
		Entry.bReady = false;
		Entry.StartedAtSeconds = NowSeconds;
		Entry.CompletedAtSeconds = 0.0;
		Entry.ExpiresAtSeconds = NowSeconds + 30.0;
		Entry.Result = FUnrealAiRetrievalQueryResult();
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("Retrieval prefetch started project_id=%s thread_id=%s turn_key=%s"),
		*InQuery.ProjectId,
		*InQuery.ThreadId,
		*TurnKey);

	Async(EAsyncExecution::ThreadPool, [this, InQuery, TurnKey, QueryHash]()
	{
		const double WorkStart = FPlatformTime::Seconds();
		const FUnrealAiRetrievalQueryResult PrefetchResult = this->Query(InQuery);
		const double WorkEnd = FPlatformTime::Seconds();

		bool bHadWarnings = PrefetchResult.Warnings.Num() > 0;
		{
			FScopeLock Lock(&PrefetchMutex);
			FRetrievalPrefetchEntry* Entry = PrefetchByTurnKey.Find(TurnKey);
			if (!Entry || Entry->QueryHash != QueryHash)
			{
				return;
			}
			Entry->Result = PrefetchResult;
			Entry->bInFlight = false;
			Entry->bReady = true;
			Entry->CompletedAtSeconds = WorkEnd;
			Entry->ExpiresAtSeconds = WorkEnd + 20.0;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("Retrieval prefetch ready project_id=%s thread_id=%s turn_key=%s ready_ms=%.0f snippets=%d warnings=%d"),
			*InQuery.ProjectId,
			*InQuery.ThreadId,
			*TurnKey,
			(WorkEnd - WorkStart) * 1000.0,
			PrefetchResult.Snippets.Num(),
			PrefetchResult.Warnings.Num());
		if (bHadWarnings)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("Retrieval prefetch completed with warnings project_id=%s thread_id=%s turn_key=%s"),
				*InQuery.ProjectId,
				*InQuery.ThreadId,
				*TurnKey);
		}
	});
}

bool FUnrealAiRetrievalService::TryConsumePrefetch(
	const FString& TurnKey,
	FUnrealAiRetrievalQueryResult& OutResult,
	bool& bOutReady)
{
	bOutReady = false;
	OutResult = FUnrealAiRetrievalQueryResult();
	if (TurnKey.IsEmpty())
	{
		return false;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	FScopeLock Lock(&PrefetchMutex);
	for (auto It = PrefetchByTurnKey.CreateIterator(); It; ++It)
	{
		if (It.Value().ExpiresAtSeconds <= NowSeconds)
		{
			It.RemoveCurrent();
		}
	}

	FRetrievalPrefetchEntry* Entry = PrefetchByTurnKey.Find(TurnKey);
	if (!Entry)
	{
		return false;
	}
	if (!Entry->bReady)
	{
		bOutReady = false;
		return true;
	}

	bOutReady = true;
	OutResult = Entry->Result;
	UE_LOG(
		LogTemp,
		Display,
		TEXT("Retrieval prefetch consumed project_id=%s thread_id=%s turn_key=%s snippets=%d warnings=%d"),
		*Entry->ProjectId,
		*Entry->ThreadId,
		*TurnKey,
		OutResult.Snippets.Num(),
		OutResult.Warnings.Num());
	return true;
}

void FUnrealAiRetrievalService::CancelPrefetchForThread(const FString& ProjectId, const FString& ThreadId)
{
	if (ProjectId.IsEmpty() || ThreadId.IsEmpty())
	{
		return;
	}
	FScopeLock Lock(&PrefetchMutex);
	for (auto It = PrefetchByTurnKey.CreateIterator(); It; ++It)
	{
		if (It.Value().ProjectId == ProjectId && It.Value().ThreadId == ThreadId)
		{
			It.RemoveCurrent();
		}
	}
}

void FUnrealAiRetrievalService::EnsureBackgroundIndexBuild(const FString& ProjectId, const FUnrealAiRetrievalSettings& Settings)
{
	if (ProjectId.IsEmpty())
	{
		return;
	}
	{
		FUnrealAiVectorIndexStore CooldownProbe(ProjectId);
		FUnrealAiVectorManifest ProbeManifest;
		if (CooldownProbe.LoadManifest(ProbeManifest)
			&& ProbeManifest.VectorDbOpenRetryNotBeforeUtc > FDateTime::UtcNow())
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("Retrieval index rebuild skipped: vector DB cooldown active until %s (project_id=%s)"),
				*ProbeManifest.VectorDbOpenRetryNotBeforeUtc.ToIso8601(),
				*ProjectId);
			return;
		}
	}
	{
		FScopeLock Lock(&StoreCacheMutex);
		if (IndexBuildsInFlight.Contains(ProjectId))
		{
			return;
		}
		IndexBuildsInFlight.Add(ProjectId);
		CachedStoresByProject.Remove(ProjectId);
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
			Manifest.VectorDbOpenRetryNotBeforeUtc = FDateTime::UtcNow() + FTimespan::FromMinutes(15);
			Store.SaveManifest(Manifest);
			TArray<FUnrealAiRetrievalDiagnosticsRow> Empty;
			UnrealAiRetrievalDiagnostics::WriteIndexDiagnostics(
				ProjectId,
				TEXT("error"),
				Manifest.FilesIndexed,
				Manifest.ChunksIndexed,
				Empty);
		}
		else
		{
			FUnrealAiVectorIndexStore Store(ProjectId);
			FUnrealAiVectorManifest Manifest;
			if (Store.LoadManifest(Manifest))
			{
				Manifest.VectorDbOpenRetryNotBeforeUtc = FDateTime::MinValue();
				Store.SaveManifest(Manifest);
			}
		}
		FScopeLock Lock(&StoreCacheMutex);
		IndexBuildsInFlight.Remove(ProjectId);
	});
}

void FUnrealAiRetrievalService::ChunkFileText(
	const FString& RelativePath,
	const FString& Text,
	int32 ChunkChars,
	int32 OverlapChars,
	TArray<FUnrealAiVectorChunkRow>& OutChunks) const
{
	ClampChunkParams(ChunkChars, OverlapChars);
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

void FUnrealAiRetrievalService::CollectBlueprintFeatureChunks(
	const FUnrealAiRetrievalSettings& Settings,
	TArray<FUnrealAiVectorChunkRow>& OutChunks) const
{
	TArray<FUnrealAiBlueprintFeatureRecord> Records;
	const int32 BpCap = Settings.BlueprintMaxFeatureRecords;
	if (IsInGameThread())
	{
		FUnrealAiBlueprintFeatureExtractor::ExtractFeatureRecords(Records, BpCap);
	}
	else
	{
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(false);
		AsyncTask(ENamedThreads::GameThread, [&Records, Done, BpCap]()
		{
			FUnrealAiBlueprintFeatureExtractor::ExtractFeatureRecords(Records, BpCap);
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
	int32 ChunkChars = Settings.ChunkChars;
	int32 ChunkOv = Settings.ChunkOverlap;
	ClampChunkParams(ChunkChars, ChunkOv);
	const int32 MaxCharsPerRecord = FMath::Max(256, ChunkChars);
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

void FUnrealAiRetrievalService::CollectSceneActorChunks(
	const FUnrealAiRetrievalSettings& Settings,
	TArray<FUnrealAiVectorChunkRow>& OutChunks) const
{
#if WITH_EDITOR
	FString SceneSourcePath;
	FString SceneText;
	auto GatherScene = [&SceneSourcePath, &SceneText]()
	{
		if (!GEditor)
		{
			return;
		}
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return;
		}
		const FString WorldPackage = World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT("UnknownWorld");
		SceneSourcePath = FString::Printf(TEXT("virtual://scene/%s"), *WorldPackage);
		constexpr int32 MaxActors = 4000;
		int32 Count = 0;
		TStringBuilder<8192> B;
		B.Appendf(TEXT("scene_world=%s\n"), *WorldPackage);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (Count >= MaxActors)
			{
				break;
			}
			AActor* A = *It;
			if (!A)
			{
				continue;
			}
			const FVector L = A->GetActorLocation();
			B.Appendf(
				TEXT("actor label=%s class=%s path=%s loc=(%.1f,%.1f,%.1f)\n"),
				*A->GetActorLabel(),
				*A->GetClass()->GetName(),
				*A->GetPathName(),
				L.X,
				L.Y,
				L.Z);
			++Count;
		}
		B.Appendf(TEXT("scene_actor_count=%d\n"), Count);
		SceneText = B.ToString();
	};

	if (IsInGameThread())
	{
		GatherScene();
	}
	else
	{
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(false);
		AsyncTask(ENamedThreads::GameThread, [&GatherScene, Done]()
		{
			GatherScene();
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

	if (SceneSourcePath.IsEmpty() || SceneText.IsEmpty())
	{
		return;
	}
	int32 ChunkChars = Settings.ChunkChars;
	int32 ChunkOv = Settings.ChunkOverlap;
	ClampChunkParams(ChunkChars, ChunkOv);
	TArray<FUnrealAiVectorChunkRow> SceneChunks;
	ChunkFileText(SceneSourcePath, SceneText, ChunkChars, ChunkOv, SceneChunks);
	OutChunks.Append(MoveTemp(SceneChunks));
#endif
}

void FUnrealAiRetrievalService::GatherAssetRegistryShardTexts(
	const FUnrealAiRetrievalSettings& Settings,
	TArray<TPair<FString, FString>>& OutVirtualPathAndFullText) const
{
#if WITH_EDITOR
	if (Settings.AssetRegistryMaxAssets <= 0)
	{
		return;
	}

	FEvent* Done = FPlatformProcess::GetSynchEventFromPool(false);
	const FUnrealAiRetrievalSettings SettingsCopy = Settings;
	AsyncTask(ENamedThreads::GameThread, [SettingsCopy, &OutVirtualPathAndFullText, Done]()
	{
		FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = RegistryModule.Get();
		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
		if (SettingsCopy.bAssetRegistryIncludeEngineAssets)
		{
			Filter.PackagePaths.Add(FName(TEXT("/Engine")));
		}
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString();
		});

		const int32 MaxAssets = SettingsCopy.AssetRegistryMaxAssets;
		const int32 Count = FMath::Min(Assets.Num(), MaxAssets);
		constexpr int32 AssetsPerShard = 500;
		int32 ShardIdx = 0;
		for (int32 Base = 0; Base < Count; Base += AssetsPerShard)
		{
			const int32 End = FMath::Min(Base + AssetsPerShard, Count);
			FString ShardText;
			for (int32 i = Base; i < End; ++i)
			{
				const FAssetData& Asset = Assets[i];
				ShardText += FString::Printf(
					TEXT("%s\t%s\t%s\n"),
					*Asset.GetSoftObjectPath().ToString(),
					*Asset.AssetClassPath.ToString(),
					*Asset.PackageName.ToString());
			}
			const FString VPath = FString::Printf(TEXT("virtual://asset_registry/part_%04d.txt"), ShardIdx++);
			OutVirtualPathAndFullText.Add(TPair<FString, FString>(VPath, ShardText));
		}
		if (Assets.Num() > MaxAssets)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("Retrieval asset registry snapshot truncated: total_assets=%d cap=%d"),
				Assets.Num(),
				MaxAssets);
		}
		Done->Trigger();
	});
	if (Done)
	{
		Done->Wait();
		FPlatformProcess::ReturnSynchEventToPool(Done);
	}
#endif
}

void FUnrealAiRetrievalService::CollectMemoryChunks(const FUnrealAiRetrievalSettings& Settings, TArray<FUnrealAiVectorChunkRow>& OutChunks) const
{
	if (!Settings.bIndexMemoryRecordsInVectorStore || !MemoryService)
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

void FUnrealAiRetrievalService::CollectDirectorySummaryChunks(
	const TMap<FString, FString>& SourceHashesByPath,
	TArray<FUnrealAiVectorChunkRow>& OutChunks) const
{
	TMap<FString, TArray<FString>> PathsByDirectory;
	for (const TPair<FString, FString>& Pair : SourceHashesByPath)
	{
		const FString& SourcePath = Pair.Key;
		if (SourcePath.StartsWith(TEXT("virtual://")))
		{
			continue;
		}
		const FString Dir = FPaths::GetPath(SourcePath);
		if (Dir.IsEmpty())
		{
			continue;
		}
		PathsByDirectory.FindOrAdd(Dir).Add(SourcePath);
	}

	TArray<FString> Directories;
	PathsByDirectory.GetKeys(Directories);
	Directories.Sort();
	const int32 MaxDirectorySummaries = 800;
	for (int32 i = 0; i < Directories.Num() && i < MaxDirectorySummaries; ++i)
	{
		const FString& Dir = Directories[i];
		TArray<FString>& Members = PathsByDirectory.FindChecked(Dir);
		Members.Sort();
		const int32 Count = Members.Num();
		if (Count <= 0)
		{
			continue;
		}
		const FString First = Members[0];
		const FString Mid = Members[Count / 2];
		const FString Last = Members[Count - 1];
		const FString Text = FString::Printf(
			TEXT("Directory summary (L0/L1): path=%s count=%d examples=[%s, %s, %s]"),
			*Dir,
			Count,
			*First,
			*Mid,
			*Last);

		FUnrealAiVectorChunkRow Row;
		Row.SourcePath = FString::Printf(TEXT("virtual://summary/directory%s"), *Dir);
		Row.Text = Text;
		Row.ContentHash = FMD5::HashAnsiString(*Text);
		Row.ChunkId = MakeChunkId(Row.SourcePath, 0, Text);
		OutChunks.Add(MoveTemp(Row));
	}
}

void FUnrealAiRetrievalService::CollectAssetEquivalenceSummaryChunks(
	const TMap<FString, FString>& SourceHashesByPath,
	TArray<FUnrealAiVectorChunkRow>& OutChunks) const
{
	struct FAssetGroup
	{
		TArray<FString> Members;
	};
	TMap<FString, FAssetGroup> Groups;

	auto NormalizeName = [](FString Name) -> FString
	{
		Name = Name.ToLower();
		const TArray<FString> Prefixes = { TEXT("bp_"), TEXT("mi_"), TEXT("m_"), TEXT("sm_"), TEXT("sk_"), TEXT("t_") };
		for (const FString& Prefix : Prefixes)
		{
			if (Name.StartsWith(Prefix))
			{
				Name = Name.RightChop(Prefix.Len());
				break;
			}
		}
		const TArray<FString> Suffixes = { TEXT("_c"), TEXT("_inst"), TEXT("_instance"), TEXT("_lod0"), TEXT("_lod1") };
		for (const FString& Suffix : Suffixes)
		{
			if (Name.EndsWith(Suffix))
			{
				Name.LeftChopInline(Suffix.Len());
				break;
			}
		}
		return Name.IsEmpty() ? TEXT("unknown") : Name;
	};
	auto ClassFamilyFromName = [](const FString& AssetName) -> FString
	{
		const FString Lower = AssetName.ToLower();
		if (Lower.StartsWith(TEXT("bp_"))) return TEXT("blueprint");
		if (Lower.StartsWith(TEXT("mi_"))) return TEXT("material_instance");
		if (Lower.StartsWith(TEXT("m_"))) return TEXT("material");
		if (Lower.StartsWith(TEXT("sm_"))) return TEXT("static_mesh");
		if (Lower.StartsWith(TEXT("sk_"))) return TEXT("skeletal_mesh");
		if (Lower.StartsWith(TEXT("t_"))) return TEXT("texture");
		return TEXT("asset");
	};

	for (const TPair<FString, FString>& Pair : SourceHashesByPath)
	{
		const FString& SourcePath = Pair.Key;
		if (!(SourcePath.StartsWith(TEXT("/Game/")) || SourcePath.StartsWith(TEXT("/Engine/"))))
		{
			continue;
		}
		FString AssetPath = SourcePath;
		int32 Dot = INDEX_NONE;
		if (AssetPath.FindChar(TEXT('.'), Dot))
		{
			AssetPath = AssetPath.Left(Dot);
		}
		const FString Folder = FPaths::GetPath(AssetPath);
		const FString AssetName = FPaths::GetBaseFilename(AssetPath, false);
		if (Folder.IsEmpty() || AssetName.IsEmpty())
		{
			continue;
		}
		const FString ClassFamily = ClassFamilyFromName(AssetName);
		const FString Equivalence = NormalizeName(AssetName);
		const FString GroupKey = FString::Printf(TEXT("%s|%s|%s"), *Folder, *ClassFamily, *Equivalence);
		Groups.FindOrAdd(GroupKey).Members.Add(AssetPath);
	}

	TArray<FString> GroupKeys;
	Groups.GetKeys(GroupKeys);
	GroupKeys.Sort();
	const int32 MaxAssetSummaryGroups = 1200;
	for (int32 i = 0; i < GroupKeys.Num() && i < MaxAssetSummaryGroups; ++i)
	{
		const FString& Key = GroupKeys[i];
		const int32 FirstSep = Key.Find(TEXT("|"));
		const int32 LastSep = Key.Find(TEXT("|"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (FirstSep == INDEX_NONE || LastSep == INDEX_NONE || LastSep <= FirstSep)
		{
			continue;
		}
		const FString Folder = Key.Left(FirstSep);
		const FString ClassFamily = Key.Mid(FirstSep + 1, LastSep - FirstSep - 1);
		const FString Equivalence = Key.Mid(LastSep + 1);

		TArray<FString>& Members = Groups.FindChecked(Key).Members;
		Members.Sort();
		const int32 Count = Members.Num();
		if (Count <= 0)
		{
			continue;
		}
		const FString First = Members[0];
		const FString Mid = Members[Count / 2];
		const FString Last = Members[Count - 1];
		const FString Text = FString::Printf(
			TEXT("Asset family summary (L0/L1): folder=%s class=%s equivalence=%s count=%d examples=[%s, %s, %s]"),
			*Folder,
			*ClassFamily,
			*Equivalence,
			Count,
			*First,
			*Mid,
			*Last);

		FUnrealAiVectorChunkRow Row;
		Row.SourcePath = FString::Printf(TEXT("virtual://summary/asset_family%s/%s/%s"), *Folder, *ClassFamily, *Equivalence);
		Row.Text = Text;
		Row.ContentHash = FMD5::HashAnsiString(*Text);
		Row.ChunkId = MakeChunkId(Row.SourcePath, 0, Text);
		OutChunks.Add(MoveTemp(Row));
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
	int32 SkippedFilesDueToCap = 0;
	TArray<FString> Files;
	UnrealAiRetrievalIndexConfig::CollectFilesystemIndexPaths(ProjectDir, Settings, Files, SkippedFilesDueToCap);
	if (SkippedFilesDueToCap > 0)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Retrieval index: skipped %d filesystem files due to maxFilesPerRebuild cap."),
			SkippedFilesDueToCap);
	}

	int32 ChunkChars = Settings.ChunkChars;
	int32 ChunkOverlap = Settings.ChunkOverlap;
	ClampChunkParams(ChunkChars, ChunkOverlap);

	TMap<FString, FString> ExistingSourceHashes;
	Store.LoadSourceHashes(ExistingSourceHashes, OutError);
	TMap<FString, FString> NewSourceHashes;
	TMap<FString, TArray<FUnrealAiVectorChunkRow>> ChangedChunksBySource;
	TArray<FString> RemovedSources;
	ExistingSourceHashes.GetKeys(RemovedSources);
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
			ChunkFileText(RelPath, Text, ChunkChars, ChunkOverlap, ChunksForSource);
			ChangedChunksBySource.Add(RelPath, MoveTemp(ChunksForSource));
		}
	}
	{
		TArray<TPair<FString, FString>> RegistryShards;
		GatherAssetRegistryShardTexts(Settings, RegistryShards);
		for (const TPair<FString, FString>& Pair : RegistryShards)
		{
			const FString& VPath = Pair.Key;
			const FString& ShardText = Pair.Value;
			const FString SourceHash = FMD5::HashAnsiString(*ShardText);
			NewSourceHashes.Add(VPath, SourceHash);
			RemovedSources.Remove(VPath);
			const FString* ExistingHash = ExistingSourceHashes.Find(VPath);
			const bool bChanged = !ExistingHash || *ExistingHash != SourceHash;
			if (bChanged)
			{
				++ChangedFiles;
				TArray<FUnrealAiVectorChunkRow> ChunksForSource;
				ChunkFileText(VPath, ShardText, ChunkChars, ChunkOverlap, ChunksForSource);
				ChangedChunksBySource.Add(VPath, MoveTemp(ChunksForSource));
			}
		}
	}
	{
		TArray<FUnrealAiVectorChunkRow> BlueprintChunks;
		CollectBlueprintFeatureChunks(Settings, BlueprintChunks);
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
		TArray<FUnrealAiVectorChunkRow> SceneChunks;
		CollectSceneActorChunks(Settings, SceneChunks);
		for (FUnrealAiVectorChunkRow& Row : SceneChunks)
		{
			const FString SourceHash = Row.ContentHash;
			NewSourceHashes.Add(Row.SourcePath, SourceHash);
			RemovedSources.Remove(Row.SourcePath);
			const FString* ExistingHash = ExistingSourceHashes.Find(Row.SourcePath);
			const bool bChanged = !ExistingHash || *ExistingHash != SourceHash;
			if (bChanged)
			{
				++ChangedFiles;
				TArray<FUnrealAiVectorChunkRow>& Rows = ChangedChunksBySource.FindOrAdd(Row.SourcePath);
				Rows.Add(MoveTemp(Row));
			}
		}
	}
	{
		TArray<FUnrealAiVectorChunkRow> MemoryChunks;
		CollectMemoryChunks(Settings, MemoryChunks);
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
	{
		TArray<FUnrealAiVectorChunkRow> DirectorySummaryChunks;
		CollectDirectorySummaryChunks(NewSourceHashes, DirectorySummaryChunks);
		for (FUnrealAiVectorChunkRow& Row : DirectorySummaryChunks)
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
		TArray<FUnrealAiVectorChunkRow> AssetFamilySummaryChunks;
		CollectAssetEquivalenceSummaryChunks(NewSourceHashes, AssetFamilySummaryChunks);
		for (FUnrealAiVectorChunkRow& Row : AssetFamilySummaryChunks)
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

	int32 MaxChunksSetting = Settings.MaxTotalChunksPerRebuild;
	int32 MaxEmbSetting = Settings.MaxEmbeddingCallsPerRebuild;
	int32 EffectiveChunkCap = MAX_int32;
	if (MaxChunksSetting > 0)
	{
		EffectiveChunkCap = FMath::Min(EffectiveChunkCap, MaxChunksSetting);
	}
	if (MaxEmbSetting > 0)
	{
		EffectiveChunkCap = FMath::Min(EffectiveChunkCap, MaxEmbSetting);
	}
	if (EffectiveChunkCap < MAX_int32 && ChangedChunksBySource.Num() > 0)
	{
		TArray<FString> SortedChangedKeys;
		ChangedChunksBySource.GetKeys(SortedChangedKeys);
		SortedChangedKeys.Sort();
		int32 RunningChunks = 0;
		int32 CutIndex = SortedChangedKeys.Num();
		for (int32 i = 0; i < SortedChangedKeys.Num(); ++i)
		{
			const TArray<FUnrealAiVectorChunkRow>* Chunks = ChangedChunksBySource.Find(SortedChangedKeys[i]);
			const int32 N = Chunks ? Chunks->Num() : 0;
			if (RunningChunks + N > EffectiveChunkCap)
			{
				CutIndex = i;
				break;
			}
			RunningChunks += N;
		}
		if (CutIndex < SortedChangedKeys.Num())
		{
			int32 DeferredChunks = 0;
			for (int32 i = CutIndex; i < SortedChangedKeys.Num(); ++i)
			{
				const FString& K = SortedChangedKeys[i];
				if (const TArray<FUnrealAiVectorChunkRow>* Ch = ChangedChunksBySource.Find(K))
				{
					DeferredChunks += Ch->Num();
				}
				ChangedChunksBySource.Remove(K);
				if (const FString* OldH = ExistingSourceHashes.Find(K))
				{
					NewSourceHashes.Add(K, *OldH);
				}
				else
				{
					NewSourceHashes.Remove(K);
				}
			}
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("Retrieval index: deferred %d changed sources (%d chunks) due to max chunk/embed cap (effective_cap=%d)."),
				SortedChangedKeys.Num() - CutIndex,
				DeferredChunks,
				EffectiveChunkCap);
			ChangedFiles = FMath::Max(0, ChangedFiles - (SortedChangedKeys.Num() - CutIndex));
		}
	}

	int32 InBatch = 0;
	const int32 EmbedBatchSize = FMath::Max(1, Settings.EmbeddingBatchSize);
	const int32 DelayMs = Settings.MinDelayMsBetweenEmbeddingBatches;

	for (TPair<FString, TArray<FUnrealAiVectorChunkRow>>& Pair : ChangedChunksBySource)
	{
		for (FUnrealAiVectorChunkRow& Chunk : Pair.Value)
		{
			FUnrealAiEmbeddingRequest Req;
			Req.InputText = Chunk.Text.Left(6000);
			Req.bBackgroundIndexer = true;
			FUnrealAiEmbeddingResponse Resp;
			if (!EmbeddingProvider.IsValid() || !EmbeddingProvider->EmbedOne(Settings.EmbeddingModel, Req, Resp))
			{
				OutError = Resp.Error.IsEmpty() ? TEXT("Embedding generation failed during indexing.") : Resp.Error;
				Manifest.Status = TEXT("error");
				Store.SaveManifest(Manifest);
				return false;
			}
			Chunk.Embedding = MoveTemp(Resp.Vector);
			++InBatch;
			if (InBatch >= EmbedBatchSize)
			{
				if (DelayMs > 0)
				{
					FPlatformProcess::Sleep(static_cast<float>(DelayMs) / 1000.f);
				}
				InBatch = 0;
			}
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
