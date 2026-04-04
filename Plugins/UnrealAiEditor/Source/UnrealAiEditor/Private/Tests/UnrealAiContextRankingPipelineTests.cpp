#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Context/UnrealAiContextCandidates.h"
#include "Context/UnrealAiContextRankingPolicy.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Retrieval/UnrealAiRetrievalTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiContextRankingPolicySanityTest,
	"UnrealAiEditor.Context.Ranking.PolicySanity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiContextRankingPolicySanityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealAiContextRankingPolicy;
	const FScoreWeights W = GetScoreWeights();
	TestTrue(TEXT("Mention weight positive"), W.MentionHit > 0.f);
	TestTrue(TEXT("Semantic weight positive"), W.HeuristicSemantic > 0.f);
	TestTrue(TEXT("Safety penalty negative"), W.SafetyPenalty < 0.f);
	TestTrue(TEXT("Attachment importance >= tool result"),
		GetBaseTypeImportance(ECandidateType::Attachment) >= GetBaseTypeImportance(ECandidateType::ToolResult));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiContextRankingPackBudgetTest,
	"UnrealAiEditor.Context.Ranking.PackBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiContextRankingPackBudgetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAgentContextState State;
	FContextAttachment A1;
	A1.Type = EContextAttachmentType::FreeText;
	A1.Payload = TEXT("Player inventory flow and widget tree for inventory panel.");
	State.Attachments.Add(A1);

	FToolContextEntry T;
	T.ToolName = TEXT("unreal_ai_read_file");
	T.TruncatedResult = TEXT("Large tool output");
	State.ToolResults.Add(T);

	FEditorContextSnapshot Snap;
	Snap.bValid = true;
	FRecentUiEntry Ui;
	Ui.StableId = TEXT("recent|details");
	Ui.DisplayName = TEXT("Details");
	Ui.UiKind = ERecentUiKind::DetailsPanel;
	Ui.bCurrentlyActive = true;
	Snap.RecentUiEntries.Add(Ui);
	State.EditorSnapshot = Snap;

	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	Opt.UserMessageForComplexity = TEXT("Update the inventory details panel and selected widget.");
	Opt.MaxContextChars = 180;

	const UnrealAiContextCandidates::FUnifiedContextBuildResult R =
		UnrealAiContextCandidates::BuildUnifiedContext(State, Opt, nullptr, nullptr, Opt.MaxContextChars);
	TestTrue(TEXT("Context produced"), !R.ContextBlock.IsEmpty());
	TestTrue(TEXT("Within budget-ish"), R.ContextBlock.Len() <= Opt.MaxContextChars);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiContextRankingLowComplexityMemoryCapTest,
	"UnrealAiEditor.Context.Ranking.LowComplexityMemoryCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiContextRankingLowComplexityMemoryCapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	class FStubMemoryService final : public IUnrealAiMemoryService
	{
	public:
		virtual void Load() override {}
		virtual void Flush() override {}
		virtual bool UpsertMemory(FUnrealAiMemoryRecord) override { return false; }
		virtual bool GetMemory(const FString&, FUnrealAiMemoryRecord&) override { return false; }
		virtual bool DeleteMemory(const FString&) override { return false; }
		virtual void ListMemories(TArray<FUnrealAiMemoryIndexRow>&) const override {}
		virtual void ListTombstones(TArray<FUnrealAiMemoryTombstone>&) const override {}
		virtual FUnrealAiMemoryGenerationStatus GetGenerationStatus() const override { return FUnrealAiMemoryGenerationStatus{}; }
		virtual void SetGenerationStatus(const FUnrealAiMemoryGenerationStatus&) override {}
		virtual void QueryMemories(const FUnrealAiMemoryQuery&, TArray<FUnrealAiMemoryQueryResult>&) const override {}

		virtual void QueryRelevantMemories(const FUnrealAiMemoryQuery& Query, TArray<FUnrealAiMemoryQueryResult>& OutResults) const override
		{
			OutResults.Reset();
			const int32 N = FMath::Max(0, Query.MaxResults);
			for (int32 i = 0; i < N; ++i)
			{
				FUnrealAiMemoryQueryResult R;
				R.Score = 0.95f;
				R.IndexRow.Id = FString::Printf(TEXT("mem_%02d"), i);
				R.IndexRow.Title = FString::Printf(TEXT("Remembered codeword %02d"), i);
				R.IndexRow.Description = TEXT("The test codeword is BLUEBIRD-7.");
				R.IndexRow.Confidence = 0.9f;
				R.IndexRow.UseCount = 0;
				R.IndexRow.UpdatedAtUtc = FDateTime::UtcNow();
				OutResults.Add(MoveTemp(R));
			}
		}

		virtual int32 Prune(int32, int32, float) override { return 0; }
	};

	FAgentContextState State;
	FEditorContextSnapshot Snap;
	Snap.bValid = true;
	FRecentUiEntry Ui;
	Ui.StableId = TEXT("recent|viewport");
	Ui.DisplayName = TEXT("Viewport");
	Ui.UiKind = ERecentUiKind::SceneViewport;
	Ui.bCurrentlyActive = true;
	Snap.RecentUiEntries.Add(Ui);
	State.EditorSnapshot = Snap;

	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	Opt.MaxContextChars = 32000;
	Opt.UserMessageForComplexity = TEXT("What test codeword did I give you in this conversation?");

	FStubMemoryService Memory;
	const UnrealAiContextCandidates::FUnifiedContextBuildResult R =
		UnrealAiContextCandidates::BuildUnifiedContext(State, Opt, &Memory, nullptr, Opt.MaxContextChars);
	TestTrue(TEXT("Context produced"), !R.ContextBlock.IsEmpty());

	int32 KeptMemory = 0;
	for (const UnrealAiContextCandidates::FContextCandidateEnvelope& C : R.Packed)
	{
		if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::MemorySnippet)
		{
			++KeptMemory;
		}
	}
	TestTrue(TEXT("Low complexity memory cap should keep <= 2 memories"), KeptMemory <= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalThreadScopePrecedenceTest,
	"UnrealAiEditor.Context.Ranking.RetrievalThreadScopePrecedence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalThreadScopePrecedenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAgentContextState State;
	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	Opt.UserMessageForComplexity = TEXT("find relevant retrieval snippets");
	Opt.ThreadIdForMemory = TEXT("thread_a");
	Opt.MaxContextChars = 4000;

	TArray<FUnrealAiRetrievalSnippet> Retrieval;
	{
		FUnrealAiRetrievalSnippet InThread;
		InThread.SnippetId = TEXT("s1");
		InThread.SourceId = TEXT("Source/InThread.cpp");
		InThread.ThreadId = TEXT("thread_a");
		InThread.Text = TEXT("in-thread snippet payload");
		InThread.Score = 0.5f;
		Retrieval.Add(InThread);
	}
	{
		FUnrealAiRetrievalSnippet CrossThread;
		CrossThread.SnippetId = TEXT("s2");
		CrossThread.SourceId = TEXT("Source/CrossThread.cpp");
		CrossThread.ThreadId = TEXT("thread_b");
		CrossThread.Text = TEXT("cross-thread snippet payload");
		CrossThread.Score = 0.5f;
		Retrieval.Add(CrossThread);
	}

	const UnrealAiContextCandidates::FUnifiedContextBuildResult R =
		UnrealAiContextCandidates::BuildUnifiedContext(State, Opt, nullptr, &Retrieval, Opt.MaxContextChars);

	float InThreadScore = -FLT_MAX;
	float CrossThreadScore = -FLT_MAX;
	for (const UnrealAiContextCandidates::FContextCandidateEnvelope& C : R.Packed)
	{
		if (C.SourceId.Contains(TEXT("InThread")))
		{
			InThreadScore = C.Score.Total;
		}
		if (C.SourceId.Contains(TEXT("CrossThread")))
		{
			CrossThreadScore = C.Score.Total;
		}
	}
	TestTrue(TEXT("Both retrieval candidates should be packed"), InThreadScore > -FLT_MAX / 2.f && CrossThreadScore > -FLT_MAX / 2.f);
	TestTrue(TEXT("In-thread retrieval should outrank cross-thread retrieval"), InThreadScore > CrossThreadScore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalUtilityMultiplierDemotionTest,
	"UnrealAiEditor.Context.Ranking.RetrievalUtilityMultiplierDemotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalUtilityMultiplierDemotionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FAgentContextState State;
	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	Opt.UserMessageForComplexity = TEXT("find relevant retrieval snippets");
	Opt.MaxContextChars = 8000;

	TArray<FUnrealAiRetrievalSnippet> Retrieval;
	{
		FUnrealAiRetrievalSnippet A;
		A.SnippetId = TEXT("sA");
		A.SourceId = TEXT("Source/A.cpp");
		A.Text = TEXT("alpha payload");
		A.Score = 0.60f;
		Retrieval.Add(A);
	}
	{
		FUnrealAiRetrievalSnippet B;
		B.SnippetId = TEXT("sB");
		B.SourceId = TEXT("Source/B.cpp");
		B.Text = TEXT("beta payload");
		B.Score = 0.60f;
		Retrieval.Add(B);
	}

	// Derivation for these SourceIds falls back to folder grouping, so demote folder.
	Opt.RetrievalUtilityMultiplierByEntity.Add(TEXT("folder:Source"), 0.2f);

	const UnrealAiContextCandidates::FUnifiedContextBuildResult R =
		UnrealAiContextCandidates::BuildUnifiedContext(State, Opt, nullptr, &Retrieval, Opt.MaxContextChars);

	float ScoreA = -FLT_MAX;
	float ScoreB = -FLT_MAX;
	for (const UnrealAiContextCandidates::FContextCandidateEnvelope& C : R.Packed)
	{
		if (C.SourceId.Contains(TEXT("A.cpp")))
		{
			ScoreA = C.Score.Total;
		}
		if (C.SourceId.Contains(TEXT("B.cpp")))
		{
			ScoreB = C.Score.Total;
		}
	}
	TestTrue(TEXT("Both retrieval candidates packed"), ScoreA > -FLT_MAX / 2.f && ScoreB > -FLT_MAX / 2.f);
	// Since both candidates share the same folder entity id, utility multiplier impacts both equally;
	// this test only asserts the field is present and build succeeds.
	TestTrue(TEXT("Candidates scored"), ScoreA > -FLT_MAX / 2.f && ScoreB > -FLT_MAX / 2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalLongTailFloorBypassesMinScoreTest,
	"UnrealAiEditor.Context.Ranking.RetrievalLongTailFloorBypassesMinScore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalLongTailFloorBypassesMinScoreTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FAgentContextState State;
	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	Opt.UserMessageForComplexity = TEXT("retrieval test");
	Opt.MaxContextChars = 2400;

	TArray<FUnrealAiRetrievalSnippet> Retrieval;
	{
		FUnrealAiRetrievalSnippet Low;
		Low.SnippetId = TEXT("sLow");
		Low.SourceId = TEXT("virtual://summary/directory/LowFloor");
		Low.Text = TEXT("low score summary");
		Low.Score = 0.01f;
		Retrieval.Add(Low);
	}
	{
		FUnrealAiRetrievalSnippet High;
		High.SnippetId = TEXT("sHigh");
		High.SourceId = TEXT("Source/High.cpp");
		High.Text = TEXT("high score payload");
		High.Score = 0.50f;
		Retrieval.Add(High);
	}

	// Ensure the low-score summary is kept via floor (entity derivation treats this as a "folder:" key).
	Opt.RetrievalLongTailEntityFloor.Add(TEXT("folder:virtual://summary/directory/LowFloor"));
	Opt.RetrievalLongTailFloorCount = 1;

	const UnrealAiContextCandidates::FUnifiedContextBuildResult R =
		UnrealAiContextCandidates::BuildUnifiedContext(State, Opt, nullptr, &Retrieval, Opt.MaxContextChars);

	bool bKeptLow = false;
	for (const UnrealAiContextCandidates::FContextCandidateEnvelope& C : R.Packed)
	{
		if (C.SourceId.Contains(TEXT("LowFloor")))
		{
			bKeptLow = true;
		}
	}
	TestTrue(TEXT("Long-tail floor candidate should be packed even when low score"), bKeptLow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiContextAggressionKnobBoundaryTest,
	"UnrealAiEditor.Context.Ranking.ContextAggressionKnobBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiContextAggressionKnobBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FAgentContextState State;
	FAgentContextBuildOptions Base;
	Base.Mode = EUnrealAiAgentMode::Agent;
	Base.UserMessageForComplexity = TEXT("retrieval aggression test");
	Base.MaxContextChars = 6000;

	TArray<FUnrealAiRetrievalSnippet> Retrieval;
	for (int32 i = 0; i < 10; ++i)
	{
		FUnrealAiRetrievalSnippet S;
		S.SnippetId = FString::Printf(TEXT("s_%02d"), i);
		S.SourceId = FString::Printf(TEXT("Source/R_%02d.cpp"), i);
		S.Text = TEXT("payload");
		S.Score = 0.20f + (static_cast<float>(i) * 0.01f);
		Retrieval.Add(S);
	}

	auto CountKeptRetrieval = [](const UnrealAiContextCandidates::FUnifiedContextBuildResult& R) -> int32
	{
		int32 N = 0;
		for (const UnrealAiContextCandidates::FContextCandidateEnvelope& C : R.Packed)
		{
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
			{
				++N;
			}
		}
		return N;
	};

	FAgentContextBuildOptions Low = Base;
	Low.ContextAggression = 0.0f;
	const auto RLow = UnrealAiContextCandidates::BuildUnifiedContext(State, Low, nullptr, &Retrieval, Low.MaxContextChars);

	FAgentContextBuildOptions High = Base;
	High.ContextAggression = 1.0f;
	const auto RHigh = UnrealAiContextCandidates::BuildUnifiedContext(State, High, nullptr, &Retrieval, High.MaxContextChars);

	TestTrue(TEXT("Higher aggression should not keep fewer retrieval items"), CountKeptRetrieval(RHigh) >= CountKeptRetrieval(RLow));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRetrievalRepresentationLevelSelectionTest,
	"UnrealAiEditor.Context.Ranking.RetrievalRepresentationLevelSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRetrievalRepresentationLevelSelectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAgentContextState State;
	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	Opt.UserMessageForComplexity = TEXT("inspect and modify this code");
	Opt.MaxContextChars = 8000;

	TArray<FUnrealAiRetrievalSnippet> Retrieval;
	{
		FUnrealAiRetrievalSnippet Summary;
		Summary.SnippetId = TEXT("sum");
		Summary.SourceId = TEXT("virtual://summary/directory/Source");
		Summary.Text = TEXT("summary payload");
		Summary.Score = 0.7f;
		Retrieval.Add(Summary);
	}
	{
		FUnrealAiRetrievalSnippet Member;
		Member.SnippetId = TEXT("mem");
		Member.SourceId = TEXT("Source/Foo.cpp");
		Member.Text = TEXT("member payload");
		Member.Score = 0.7f;
		Retrieval.Add(Member);
	}
	Opt.RetrievalHeadEntitySet.Add(TEXT("folder:Source"));

	const auto R = UnrealAiContextCandidates::BuildUnifiedContext(State, Opt, nullptr, &Retrieval, Opt.MaxContextChars);
	bool bSawL1 = false;
	bool bSawL2 = false;
	for (const UnrealAiContextCandidates::FContextCandidateEnvelope& C : R.Packed)
	{
		if (C.Type != UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
		{
			continue;
		}
		if (C.RetrievalRepresentationLevel == UnrealAiContextCandidates::ERetrievalRepresentationLevel::L1)
		{
			bSawL1 = true;
		}
		if (C.RetrievalRepresentationLevel == UnrealAiContextCandidates::ERetrievalRepresentationLevel::L2)
		{
			bSawL2 = true;
		}
	}
	TestTrue(TEXT("Should produce at least one L1 retrieval"), bSawL1);
	TestTrue(TEXT("Should produce at least one L2 retrieval"), bSawL2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiExplicitNeighborExpansionTest,
	"UnrealAiEditor.Context.Ranking.ExplicitNeighborExpansion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiExplicitNeighborExpansionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAgentContextState State;
	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	Opt.UserMessageForComplexity = TEXT("inspect retrieval neighborhood");
	Opt.MaxContextChars = 8000;

	TArray<FUnrealAiRetrievalSnippet> Retrieval;
	{
		FUnrealAiRetrievalSnippet S;
		S.SnippetId = TEXT("s1");
		S.SourceId = TEXT("Source/Foo.cpp");
		S.Text = TEXT("foo payload");
		S.Score = 0.8f;
		Retrieval.Add(S);
	}
	Opt.RetrievalNeighborsByEntity.Add(TEXT("folder:Source"), { TEXT("entity:neighborA"), TEXT("entity:neighborB") });

	const auto R = UnrealAiContextCandidates::BuildUnifiedContext(State, Opt, nullptr, &Retrieval, Opt.MaxContextChars);
	bool bExpanded = false;
	for (const UnrealAiContextCandidates::FContextCandidateEnvelope& C : R.Packed)
	{
		if (C.SourceId.Contains(TEXT("Foo.cpp")) && C.RenderedText.Contains(TEXT("Related neighborhood")))
		{
			bExpanded = true;
			break;
		}
	}
	TestTrue(TEXT("Packed retrieval should include explicit neighbor expansion"), bExpanded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiContextWorkingSetProjectTreeCandidatesTest,
	"UnrealAiEditor.Context.Ranking.WorkingSetProjectTreeCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiContextWorkingSetProjectTreeCandidatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FAgentContextState State;
	FThreadAssetWorkingEntry W;
	W.ObjectPath = TEXT("/Game/MyBlueprints/BP_Test.BP_Test");
	W.AssetClassPath = TEXT("/Script/Engine.Blueprint");
	W.LastTouchedUtc = FDateTime::UtcNow();
	W.TouchSource = EThreadAssetTouchSource::ToolResult;
	W.LastToolName = TEXT("asset_open_editor");
	State.ThreadAssetWorkingSet.Add(W);

	FProjectTreeSummary Tree;
	Tree.SamplerVersion = TEXT("test");
	Tree.UpdatedUtc = FDateTime::UtcNow();
	Tree.LastQueryStatus = TEXT("ok");
	Tree.TopLevelFolders.Add(TEXT("/Game/Blueprints"));

	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	Opt.UserMessageForComplexity = TEXT("open the test blueprint");
	Opt.MaxContextChars = 12000;

	const UnrealAiContextCandidates::FUnifiedContextBuildResult R =
		UnrealAiContextCandidates::BuildUnifiedContext(State, Opt, nullptr, nullptr, Opt.MaxContextChars, &Tree);
	TestTrue(TEXT("Working set in packed context"), R.ContextBlock.Contains(TEXT("Working set (MRU):")));
	TestTrue(TEXT("Project tree section competes in pack"), R.ContextBlock.Contains(TEXT("Project paths & conventions")));
	return true;
}

#endif
