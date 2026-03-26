#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Context/AgentContextJson.h"
#include "Context/UnrealAiRecentUiRanking.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRecentUiRankingPriorityTest,
	"UnrealAiEditor.Context.RecentUi.RankingPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRecentUiRankingPriorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FDateTime Now = FDateTime::UtcNow();

	FRecentUiEntry Details;
	Details.StableId = TEXT("details");
	Details.DisplayName = TEXT("Details");
	Details.UiKind = ERecentUiKind::DetailsPanel;
	Details.LastSeenUtc = Now - FTimespan::FromSeconds(10.0);
	Details.SeenCount = 4;

	FRecentUiEntry Output;
	Output.StableId = TEXT("output");
	Output.DisplayName = TEXT("Output Log");
	Output.UiKind = ERecentUiKind::OutputLog;
	Output.LastSeenUtc = Now - FTimespan::FromSeconds(2.0);
	Output.SeenCount = 1;

	TArray<FRecentUiEntry> Ranked;
	UnrealAiRecentUiRanking::MergeAndRank(TArray<FRecentUiEntry>{Details, Output}, TArray<FRecentUiEntry>(), 8, Ranked);

	TestEqual(TEXT("Ranked count"), Ranked.Num(), 2);
	if (Ranked.Num() >= 2)
	{
		TestEqual(TEXT("Details should outrank Output Log by importance"), Ranked[0].StableId, FString(TEXT("details")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRecentUiJsonRoundTripTest,
	"UnrealAiEditor.Context.RecentUi.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRecentUiJsonRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FAgentContextState In;
	In.SchemaVersionField = FAgentContextState::SchemaVersion;
	FEditorContextSnapshot Snap;
	Snap.bValid = true;
	Snap.ActiveUiEntryId = TEXT("asset_editor|/Game/BP_Test");
	FRecentUiEntry E;
	E.StableId = TEXT("asset_editor|/Game/BP_Test");
	E.DisplayName = TEXT("BP_Test");
	E.UiKind = ERecentUiKind::AssetEditor;
	E.Source = ERecentUiSource::SlateFocus;
	E.SeenCount = 3;
	E.bCurrentlyActive = true;
	Snap.RecentUiEntries.Add(E);
	In.EditorSnapshot = Snap;
	In.ThreadRecentUiOverlay.Add(E);

	FString Json;
	TestTrue(TEXT("StateToJson"), UnrealAiAgentContextJson::StateToJson(In, Json));

	FAgentContextState Out;
	TArray<FString> Warnings;
	TestTrue(TEXT("JsonToState"), UnrealAiAgentContextJson::JsonToState(Json, Out, Warnings));
	TestTrue(TEXT("Snapshot present"), Out.EditorSnapshot.IsSet());
	if (Out.EditorSnapshot.IsSet())
	{
		TestEqual(TEXT("RecentUiEntries count"), Out.EditorSnapshot.GetValue().RecentUiEntries.Num(), 1);
		TestEqual(TEXT("ActiveUiEntryId preserved"), Out.EditorSnapshot.GetValue().ActiveUiEntryId, FString(TEXT("asset_editor|/Game/BP_Test")));
	}
	TestEqual(TEXT("Thread overlay count"), Out.ThreadRecentUiOverlay.Num(), 1);
	return true;
}

#endif
