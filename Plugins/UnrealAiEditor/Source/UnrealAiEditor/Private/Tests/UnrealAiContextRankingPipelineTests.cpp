#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Context/UnrealAiContextCandidates.h"
#include "Context/UnrealAiContextRankingPolicy.h"

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
		UnrealAiContextCandidates::BuildUnifiedContext(State, Opt, nullptr, Opt.MaxContextChars);
	TestTrue(TEXT("Context produced"), !R.ContextBlock.IsEmpty());
	TestTrue(TEXT("Within budget-ish"), R.ContextBlock.Len() <= Opt.MaxContextChars);
	return true;
}

#endif
