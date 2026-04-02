#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#include "Backend/FUnrealAiPersistenceStub.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Retrieval/FUnrealAiRetrievalService.h"
#include "Retrieval/IUnrealAiEmbeddingProvider.h"
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
	TestEqual(TEXT("chunk chars default"), S.ChunkChars, 1200);
	TestEqual(TEXT("chunk overlap default"), S.ChunkOverlap, 200);
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
	TestTrue(TEXT("save manifest with cooldown"), Store.SaveManifest(Written));
	FUnrealAiVectorManifest Read;
	TestTrue(TEXT("load manifest"), Store.LoadManifest(Read));
	TestEqual(TEXT("status round-trip"), Read.Status, Written.Status);
	TestTrue(TEXT("cooldown persisted and in future"), Read.VectorDbOpenRetryNotBeforeUtc > FDateTime::UtcNow());
	TestTrue(
		TEXT("cooldown ~5m ahead"),
		(Read.VectorDbOpenRetryNotBeforeUtc - FDateTime::UtcNow()).GetTotalMinutes() >= 4.0);
	return true;
}

#endif
