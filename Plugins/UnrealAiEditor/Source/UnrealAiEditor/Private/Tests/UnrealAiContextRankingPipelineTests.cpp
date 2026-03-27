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

#endif
