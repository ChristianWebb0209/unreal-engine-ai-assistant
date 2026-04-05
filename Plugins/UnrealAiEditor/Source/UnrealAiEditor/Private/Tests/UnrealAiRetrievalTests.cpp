#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

#include "Backend/FUnrealAiPersistenceStub.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Retrieval/FUnrealAiRetrievalService.h"
#include "Retrieval/IUnrealAiEmbeddingProvider.h"
#include "Retrieval/UnrealAiOpenAiBatchEmbeddingsParse.h"
#include "Retrieval/UnrealAiRetrievalIndexConfig.h"
#include "Retrieval/UnrealAiVectorIndexStore.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalDisabledFallbackTest,
	"UnrealAiEditor.Retrieval.DisabledFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalDisabledFallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPersistenceStub Persist;
	FUnrealAiModelProfileRegistry Profiles(&Persist);
	FUnrealAiRetrievalService Retrieval(&Persist, &Profiles, nullptr);

	FUnrealAiRetrievalQuery Query;
	Query.ProjectId = TEXT("test_project");
	Query.ThreadId = TEXT("thread_1");
	Query.QueryText = TEXT("find relevant code");
	Query.MaxResults = 4;

	const FUnrealAiRetrievalQueryResult R = Retrieval.Query(Query);
	TestEqual(TEXT("Disabled retrieval should return zero snippets"), R.Snippets.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalIndexConfigWhitelistTest,
	"UnrealAiEditor.Retrieval.IndexConfigWhitelist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalIndexConfigWhitelistTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<FString> Legacy = UnrealAiRetrievalIndexConfig::GetLegacyDefaultIndexedExtensions();
	TestEqual(TEXT("legacy default extension count"), Legacy.Num(), 6);
	TestTrue(TEXT("legacy includes .h"), Legacy.Contains(TEXT(".h")));

	FUnrealAiRetrievalSettings Settings;
	TArray<FString> Effective;
	UnrealAiRetrievalIndexConfig::GetEffectiveIndexedExtensions(Settings, Effective);
	TestEqual(TEXT("empty indexedExtensions uses legacy list"), Effective.Num(), Legacy.Num());

	Settings.IndexedExtensions = {TEXT("CS"), TEXT(".JSON")};
	UnrealAiRetrievalIndexConfig::GetEffectiveIndexedExtensions(Settings, Effective);
	TestEqual(TEXT("custom whitelist size"), Effective.Num(), 2);
	TestTrue(TEXT("normalized .cs"), Effective.Contains(TEXT(".cs")));
	TestTrue(TEXT("normalized .json"), Effective.Contains(TEXT(".json")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalSettingsDefaultsTest,
	"UnrealAiEditor.Retrieval.SettingsDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FUnrealAiRetrievalSettings S;
	TestFalse(TEXT("memory vector index off by default"), S.bIndexMemoryRecordsInVectorStore);
	TestEqual(TEXT("asset registry off by default"), S.AssetRegistryMaxAssets, 0);
	TestEqual(TEXT("minimal preset"), S.RootPreset, EUnrealAiRetrievalRootPreset::Minimal);
	TestEqual(TEXT("chunk chars default"), S.ChunkChars, 4000);
	TestEqual(TEXT("chunk overlap default"), S.ChunkOverlap, 150);
	TestEqual(TEXT("max chunks per file default"), S.MaxChunksPerFile, 96);
	TestEqual(TEXT("embedding batch default (chunks per HTTP)"), S.EmbeddingBatchSize, 128);
	TestEqual(TEXT("first wave time budget off by default"), S.IndexFirstWaveTimeBudgetSeconds, 0);
	TestFalse(TEXT("blueprint engine off by default"), S.bBlueprintIncludeEngineAssets);
	TestTrue(TEXT("directory summaries on by default"), S.bIndexDirectorySummaries);
	TestTrue(TEXT("asset family summaries on by default"), S.bIndexAssetFamilySummaries);
	TestFalse(TEXT("editor scene off by default"), S.bIndexEditorScene);
	TestFalse(TEXT("retrieval wave 4 off by default"), S.bIndexRetrievalWave4);
	TestEqual(TEXT("max files default unlimited"), S.MaxFilesPerRebuild, 0);
	TestTrue(TEXT("indexedExtensions default empty"), S.IndexedExtensions.Num() == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalMaxFilesCapTest,
	"UnrealAiEditor.Retrieval.MaxFilesCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalMaxFilesCapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// Uses the project directory; only asserts that a strict cap is enforced and skipped count is reported.
	FUnrealAiRetrievalSettings Settings;
	Settings.RootPreset = EUnrealAiRetrievalRootPreset::Minimal;
	Settings.MaxFilesPerRebuild = 0;
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	TArray<FString> PathsAll;
	int32 SkippedAll = 0;
	UnrealAiRetrievalIndexConfig::CollectFilesystemIndexPaths(ProjectDir, Settings, PathsAll, SkippedAll);
	TestEqual(TEXT("no cap means no skip"), SkippedAll, 0);

	Settings.MaxFilesPerRebuild = 1;
	TArray<FString> PathsCapped;
	int32 Skipped = 0;
	UnrealAiRetrievalIndexConfig::CollectFilesystemIndexPaths(ProjectDir, Settings, PathsCapped, Skipped);
	TestEqual(TEXT("capped list at most 1"), PathsCapped.Num() <= 1, true);
	if (PathsAll.Num() > 1)
	{
		TestTrue(TEXT("skipped reported when cap applies"), Skipped > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalChunkCapTest,
	"UnrealAiEditor.Retrieval.ChunkCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalChunkCapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FString Big;
	for (int32 i = 0; i < 200; ++i)
	{
		Big += TEXT("abcdefgh");
	}
	TArray<FUnrealAiVectorChunkRow> Uncapped;
	UnrealAiRetrievalIndexConfig::ChunkTextFixedWindow(TEXT("test.cpp"), Big, 32, 4, 0, Uncapped);
	TArray<FUnrealAiVectorChunkRow> Capped;
	UnrealAiRetrievalIndexConfig::ChunkTextFixedWindow(TEXT("test.cpp"), Big, 32, 4, 3, Capped);
	TestTrue(TEXT("uncapped has many chunks"), Uncapped.Num() > 5);
	TestEqual(TEXT("capped at 3"), Capped.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalBuildSortPriorityTest,
	"UnrealAiEditor.Retrieval.BuildSortPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalBuildSortPriorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Base = TEXT("C:/FakeProj");
	TArray<FString> Paths = {
		FPaths::Combine(Base, TEXT("docs/readme.md")),
		FPaths::Combine(Base, TEXT("Source/Game/Module/A.cpp")),
		FPaths::Combine(Base, TEXT("Plugins/P/Source/Mod/B.cpp")),
	};
	UnrealAiRetrievalIndexConfig::SortFilesystemIndexPathsForBuildPriority(Base, Paths);
	TestTrue(TEXT("project Source before Plugins"), Paths[0].Contains(TEXT("Source/Game")));
	TestTrue(TEXT("plugin Source second"), Paths[1].Contains(TEXT("Plugins/P/Source")));
	TestTrue(TEXT("docs last of three"), Paths[2].Contains(TEXT("docs/")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalIndexBuildWaveBucketsTest,
	"UnrealAiEditor.Retrieval.IndexBuildWaveBuckets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalIndexBuildWaveBucketsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Proj = TEXT("C:/FakeProj");
	TestEqual(TEXT("wave count"), UnrealAiRetrievalIndexConfig::GetIndexBuildWaveCount(), 5);
	TestEqual(TEXT("P0 project Source"), UnrealAiRetrievalIndexConfig::GetIndexBuildWaveForSource(Proj, TEXT("Source/MyMod/Private/A.cpp")), 0);
	TestEqual(TEXT("P1 plugin Source"), UnrealAiRetrievalIndexConfig::GetIndexBuildWaveForSource(Proj, TEXT("Plugins/P/Source/Mod/B.cpp")), 1);
	TestEqual(TEXT("P2 Config"), UnrealAiRetrievalIndexConfig::GetIndexBuildWaveForSource(Proj, TEXT("Config/DefaultEngine.ini")), 2);
	TestEqual(TEXT("P2 docs"), UnrealAiRetrievalIndexConfig::GetIndexBuildWaveForSource(Proj, TEXT("docs/readme.md")), 2);
	TestEqual(TEXT("P3 Content"), UnrealAiRetrievalIndexConfig::GetIndexBuildWaveForSource(Proj, TEXT("Content/Foo/bar.uasset")), 3);
	TestEqual(TEXT("P4 virtual"), UnrealAiRetrievalIndexConfig::GetIndexBuildWaveForSource(Proj, TEXT("virtual://shard/1")), 4);
	TestEqual(TEXT("P4 memory"), UnrealAiRetrievalIndexConfig::GetIndexBuildWaveForSource(Proj, TEXT("memory:thread")), 4);
	TestEqual(TEXT("P4 asset path"), UnrealAiRetrievalIndexConfig::GetIndexBuildWaveForSource(Proj, TEXT("/Game/Some/Asset")), 4);
	TestEqual(TEXT("P4 misc root file"), UnrealAiRetrievalIndexConfig::GetIndexBuildWaveForSource(Proj, TEXT("Other/readme.txt")), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalFastIndexSettingsParseTest,
	"UnrealAiEditor.Retrieval.FastIndexSettingsParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalFastIndexSettingsParseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPersistenceStub Persist;
	FUnrealAiModelProfileRegistry Profiles(&Persist);
	FUnrealAiRetrievalService Retrieval(&Persist, &Profiles, nullptr);

	const FString SettingsJson = TEXT(
		"{\"retrieval\":{\"enabled\":true,\"embeddingModel\":\"text-embedding-3-small\","
		"\"maxChunksPerFile\":12,\"blueprintIncludeEngineAssets\":true,"
		"\"indexDirectorySummaries\":false,\"indexAssetFamilySummaries\":false,\"indexEditorScene\":true}}");
	TestTrue(TEXT("Save settings json"), Persist.SaveSettingsJson(SettingsJson));

	const FUnrealAiRetrievalSettings S = Retrieval.LoadSettings();
	TestEqual(TEXT("maxChunksPerFile parsed"), S.MaxChunksPerFile, 12);
	TestTrue(TEXT("blueprintIncludeEngineAssets"), S.bBlueprintIncludeEngineAssets);
	TestFalse(TEXT("directory summaries"), S.bIndexDirectorySummaries);
	TestFalse(TEXT("asset families"), S.bIndexAssetFamilySummaries);
	TestTrue(TEXT("editor scene"), S.bIndexEditorScene);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalFirstWaveBudgetSettingsParseTest,
	"UnrealAiEditor.Retrieval.FirstWaveBudgetSettingsParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalFirstWaveBudgetSettingsParseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPersistenceStub Persist;
	FUnrealAiModelProfileRegistry Profiles(&Persist);
	FUnrealAiRetrievalService Retrieval(&Persist, &Profiles, nullptr);
	const FString SettingsJson = TEXT("{\"retrieval\":{\"enabled\":true,\"embeddingModel\":\"text-embedding-3-small\","
										"\"indexFirstWaveTimeBudgetSeconds\":42}}");
	TestTrue(TEXT("Save settings json"), Persist.SaveSettingsJson(SettingsJson));
	const FUnrealAiRetrievalSettings S = Retrieval.LoadSettings();
	TestEqual(TEXT("first wave budget parsed"), S.IndexFirstWaveTimeBudgetSeconds, 42);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalWave4SettingsParseTest,
	"UnrealAiEditor.Retrieval.Wave4SettingsParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalWave4SettingsParseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPersistenceStub Persist;
	FUnrealAiModelProfileRegistry Profiles(&Persist);
	FUnrealAiRetrievalService Retrieval(&Persist, &Profiles, nullptr);
	const FString SettingsJson =
		TEXT("{\"retrieval\":{\"enabled\":true,\"embeddingModel\":\"text-embedding-3-small\",\"indexRetrievalWave4\":false}}");
	TestTrue(TEXT("Save settings json"), Persist.SaveSettingsJson(SettingsJson));
	const FUnrealAiRetrievalSettings S = Retrieval.LoadSettings();
	TestFalse(TEXT("indexRetrievalWave4 false"), S.bIndexRetrievalWave4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalContextAggressionSettingsParseTest,
	"UnrealAiEditor.Retrieval.ContextAggressionSettingsParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalContextAggressionSettingsParseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPersistenceStub Persist;
	FUnrealAiModelProfileRegistry Profiles(&Persist);
	FUnrealAiRetrievalService Retrieval(&Persist, &Profiles, nullptr);

	const FString SettingsJson = TEXT("{\"retrieval\":{\"enabled\":true,\"embeddingModel\":\"text-embedding-3-small\",\"contextAggression\":0.91}}");
	TestTrue(TEXT("Save settings json"), Persist.SaveSettingsJson(SettingsJson));

	const FUnrealAiRetrievalSettings Settings = Retrieval.LoadSettings();
	TestTrue(TEXT("Context aggression parsed"), FMath::IsNearlyEqual(Settings.ContextAggression, 0.91f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalEmbeddingRequestDefaultsTest,
	"UnrealAiEditor.Retrieval.EmbeddingRequestDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalEmbeddingRequestDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FUnrealAiEmbeddingRequest R;
	TestFalse(TEXT("bBackgroundIndexer defaults false"), R.bBackgroundIndexer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiOpenAiBatchEmbeddingsParseTest,
	"UnrealAiEditor.Retrieval.OpenAiBatchEmbeddingsParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiOpenAiBatchEmbeddingsParseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Json = TEXT(
		"{\"object\":\"list\",\"data\":["
		"{\"object\":\"embedding\",\"index\":1,\"embedding\":[0.5,1.0]},"
		"{\"object\":\"embedding\",\"index\":0,\"embedding\":[0.25,0.75]}"
		"]}");
	TArray<TArray<float>> Out;
	FString Err;
	TestTrue(TEXT("parse 2-item batch"), UnrealAiOpenAiBatchEmbeddingsParse::ParseEmbeddingsDataFromResponse(Json, 2, Out, Err));
	TestEqual(TEXT("vector count"), Out.Num(), 2);
	TestTrue(TEXT("first vec len"), Out[0].Num() == 2);
	TestTrue(TEXT("second vec len"), Out[1].Num() == 2);
	TestTrue(TEXT("first dim0"), FMath::IsNearlyEqual(Out[0][0], 0.25f, 1.e-5f));
	TestTrue(TEXT("first dim1"), FMath::IsNearlyEqual(Out[0][1], 0.75f, 1.e-5f));
	TestTrue(TEXT("second dim0"), FMath::IsNearlyEqual(Out[1][0], 0.5f, 1.e-5f));
	TestTrue(TEXT("second dim1"), FMath::IsNearlyEqual(Out[1][1], 1.0f, 1.e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalVectorManifestCooldownRoundTripTest,
	"UnrealAiEditor.Retrieval.VectorManifestCooldownRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalVectorManifestCooldownRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString ProjectKey = FString::Printf(TEXT("autotest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
	FUnrealAiVectorIndexStore Store(ProjectKey);
	FUnrealAiVectorManifest Written;
	Written.ProjectId = ProjectKey;
	Written.Status = TEXT("error");
	Written.VectorDbOpenRetryNotBeforeUtc = FDateTime::UtcNow() + FTimespan::FromMinutes(5);
	Written.IndexBuildTargetChunks = 120;
	Written.IndexBuildCompletedChunks = 45;
	Written.IndexBuildWaveTotal = 5;
	Written.IndexBuildWaveDone = 2;
	Written.IndexBuildPhaseStartedUtc = FDateTime::UtcNow() - FTimespan::FromSeconds(30);
	TestTrue(TEXT("save manifest with cooldown"), Store.SaveManifest(Written));
	FUnrealAiVectorManifest Read;
	TestTrue(TEXT("load manifest"), Store.LoadManifest(Read));
	TestEqual(TEXT("status round-trip"), Read.Status, Written.Status);
	TestEqual(TEXT("index_build_wave_total round-trip"), Read.IndexBuildWaveTotal, Written.IndexBuildWaveTotal);
	TestEqual(TEXT("index_build_wave_done round-trip"), Read.IndexBuildWaveDone, Written.IndexBuildWaveDone);
	TestEqual(TEXT("index_build_target_chunks round-trip"), Read.IndexBuildTargetChunks, Written.IndexBuildTargetChunks);
	TestEqual(TEXT("index_build_completed_chunks round-trip"), Read.IndexBuildCompletedChunks, Written.IndexBuildCompletedChunks);
	TestTrue(
		TEXT("index_build_phase_started_utc round-trip"),
		FMath::IsNearlyEqual(
			(Read.IndexBuildPhaseStartedUtc - Written.IndexBuildPhaseStartedUtc).GetTotalSeconds(),
			0.0,
			1.0));
	TestTrue(TEXT("cooldown persisted and in future"), Read.VectorDbOpenRetryNotBeforeUtc > FDateTime::UtcNow());
	TestTrue(
		TEXT("cooldown ~5m ahead"),
		(Read.VectorDbOpenRetryNotBeforeUtc - FDateTime::UtcNow()).GetTotalMinutes() >= 4.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalGetOrCreateStoreCooldownSkipsOpenTest,
	"UnrealAiEditor.Retrieval.GetOrCreateStoreCooldownSkipsOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalGetOrCreateStoreCooldownSkipsOpenTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPersistenceStub Persist;
	FUnrealAiModelProfileRegistry Profiles(&Persist);
	TestTrue(
		TEXT("enable retrieval settings"),
		Persist.SaveSettingsJson(
			TEXT("{\"retrieval\":{\"enabled\":true,\"embeddingModel\":\"text-embedding-3-small\"}}")));

	const FString ProjectKey = FString::Printf(TEXT("autotest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
	FUnrealAiVectorIndexStore ManifestOnly(ProjectKey);
	FUnrealAiVectorManifest CooldownManifest;
	CooldownManifest.ProjectId = ProjectKey;
	CooldownManifest.Status = TEXT("error");
	CooldownManifest.VectorDbOpenRetryNotBeforeUtc = FDateTime::UtcNow() + FTimespan::FromHours(1);
	TestTrue(TEXT("save manifest cooldown without db"), ManifestOnly.SaveManifest(CooldownManifest));

	FUnrealAiRetrievalService Retrieval(&Persist, &Profiles, nullptr);
	FUnrealAiRetrievalQuery Q;
	Q.ProjectId = ProjectKey;
	Q.ThreadId = TEXT("thread_cd");
	Q.QueryText = TEXT("sample query text");
	Q.MaxResults = 4;
	const FUnrealAiRetrievalQueryResult R = Retrieval.Query(Q);
	TestTrue(TEXT("query yields store warning"), R.Warnings.Num() > 0);
	TestTrue(TEXT("cooldown fast-fail in warning"), R.Warnings[0].Contains(TEXT("cooldown")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalStaleIndexingManifestRecoveryTest,
	"UnrealAiEditor.Retrieval.StaleIndexingManifestRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalStaleIndexingManifestRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPersistenceStub Persist;
	FUnrealAiModelProfileRegistry Profiles(&Persist);
	TestTrue(
		TEXT("enable retrieval settings"),
		Persist.SaveSettingsJson(
			TEXT("{\"retrieval\":{\"enabled\":true,\"embeddingModel\":\"text-embedding-3-small\"}}")));

	const FString ProjectKey = FString::Printf(TEXT("autotest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
	FUnrealAiVectorIndexStore Store(ProjectKey);
	FString Err;
	TestTrue(TEXT("init store"), Store.Initialize(Err));
	FUnrealAiVectorChunkRow Row;
	Row.ChunkId = TEXT("c1");
	Row.SourcePath = TEXT("/Game/Test.txt");
	Row.Text = TEXT("alpha beta gamma");
	Row.ContentHash = FMD5::HashAnsiString(*Row.Text);
	Row.Embedding = {0.1f, 0.2f, 0.3f};
	TArray<FUnrealAiVectorChunkRow> Rows;
	Rows.Add(Row);
	TestTrue(TEXT("upsert chunks"), Store.UpsertAllChunks(Rows, Err));
	FUnrealAiVectorManifest Interrupted;
	Interrupted.ProjectId = ProjectKey;
	Interrupted.Status = TEXT("indexing");
	Interrupted.IndexBuildTargetChunks = 50;
	Interrupted.IndexBuildCompletedChunks = 12;
	Interrupted.IndexBuildPhaseStartedUtc = FDateTime::UtcNow();
	Interrupted.EmbeddingModel = TEXT("text-embedding-3-small");
	TestTrue(TEXT("save interrupted manifest"), Store.SaveManifest(Interrupted));

	FUnrealAiRetrievalService Retrieval(&Persist, &Profiles, nullptr);
	const FUnrealAiRetrievalProjectStatus St = Retrieval.GetProjectStatus(ProjectKey);
	TestEqual(TEXT("normalized status"), St.StateText, FString(TEXT("ready")));
	TestEqual(TEXT("cleared embed target"), St.IndexBuildTargetChunks, 0);
	TestEqual(TEXT("cleared embed completed"), St.IndexBuildCompletedChunks, 0);
	TestTrue(TEXT("phase cleared"), St.IndexBuildPhaseStartedUtc == FDateTime::MinValue());
	TestEqual(TEXT("chunk count from db"), St.ChunksIndexed, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalUnusableEmbeddingDetectionTest,
	"UnrealAiEditor.Retrieval.UnusableEmbeddingDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalUnusableEmbeddingDetectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString ProjectKey = FString::Printf(TEXT("autotest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
	FUnrealAiVectorIndexStore Store(ProjectKey);
	FString Err;
	TestTrue(TEXT("init store"), Store.Initialize(Err));
	FUnrealAiVectorChunkRow Row;
	Row.ChunkId = TEXT("c_empty_emb");
	Row.SourcePath = TEXT("/Game/Broken.cpp");
	Row.Text = TEXT("hello");
	Row.ContentHash = FMD5::HashAnsiString(*Row.Text);
	Row.Embedding.Reset();
	TArray<FUnrealAiVectorChunkRow> Rows;
	Rows.Add(Row);
	TestTrue(TEXT("upsert with empty embedding"), Store.UpsertAllChunks(Rows, Err));
	int32 Bad = 0;
	TestTrue(TEXT("scan embeddings"), Store.CountChunksWithUnusableEmbeddings(Bad, Err));
	TestEqual(TEXT("detects empty vector json"), Bad, 1);
	TestTrue(TEXT("wipe project chunks"), Store.DeleteAllChunksForProject(Err));
	int32 Files = 0;
	int32 Chunks = 0;
	TestTrue(TEXT("counts after wipe"), Store.GetIndexCounts(Files, Chunks, Err));
	TestEqual(TEXT("no chunks"), Chunks, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalIncrementalPartialThenRemoveTest,
	"UnrealAiEditor.Retrieval.IncrementalPartialThenRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalIncrementalPartialThenRemoveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString ProjectKey = FString::Printf(TEXT("autotest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
	FUnrealAiVectorIndexStore Store(ProjectKey);
	FString Err;
	TestTrue(TEXT("init store"), Store.Initialize(Err));

	FUnrealAiVectorChunkRow Kept;
	Kept.ChunkId = TEXT("kept1");
	Kept.SourcePath = TEXT("Source/Priority/P0.cpp");
	Kept.Text = TEXT("unambiguous_token_alpha_beta");
	Kept.ContentHash = FMD5::HashAnsiString(*Kept.Text);
	Kept.Embedding = {0.9f, 0.1f, 0.0f};

	TArray<FUnrealAiVectorChunkRow> KeptRows;
	KeptRows.Add(Kept);
	TMap<FString, TArray<FUnrealAiVectorChunkRow>> Wave1;
	Wave1.Add(Kept.SourcePath, KeptRows);
	TMap<FString, FString> Hashes1;
	Hashes1.Add(Kept.SourcePath, TEXT("hash_a"));

	TestTrue(TEXT("incremental wave without removals"), Store.UpsertIncremental(Hashes1, Wave1, TArray<FString>(), Err));
	int32 Files = 0;
	int32 Chunks = 0;
	TestTrue(TEXT("counts after partial upsert"), Store.GetIndexCounts(Files, Chunks, Err));
	TestEqual(TEXT("one chunk visible"), Chunks, 1);

	TArray<FUnrealAiRetrievalSnippet> LexHit;
	TestTrue(TEXT("lexical query hits committed chunk"),
		Store.QueryTopKByLexical(TEXT("unambiguous_token_alpha_beta"), 4, LexHit, Err));
	TestTrue(TEXT("at least one snippet"), LexHit.Num() > 0);

	const TArray<FString> Removed{Kept.SourcePath};
	TMap<FString, FString> NoHashes;
	TMap<FString, TArray<FUnrealAiVectorChunkRow>> NoChunks;
	TestTrue(TEXT("delete-only incremental"), Store.UpsertIncremental(NoHashes, NoChunks, Removed, Err));
	TestTrue(TEXT("counts after remove"), Store.GetIndexCounts(Files, Chunks, Err));
	TestEqual(TEXT("chunks removed"), Chunks, 0);
	return true;
}

#endif
