#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Harness/FUnrealAiWorkerOrchestrator.h"
#include "Harness/UnrealAiPlanDag.h"
#include "Harness/UnrealAiAgentTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiMergeDeterministicTest,
	"UnrealAiEditor.Harness.MergeDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiMergeDeterministicTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiWorkerResult A;
	A.Status = TEXT("success");
	A.Summary = TEXT("alpha");
	FUnrealAiWorkerResult B;
	B.Status = TEXT("failed");
	B.Summary = TEXT("beta");
	B.Errors.Add(TEXT("e1"));

	const FUnrealAiWorkerResult M = FUnrealAiWorkerOrchestrator::MergeDeterministic({A, B});
	TestEqual(TEXT("status partial"), M.Status, FString(TEXT("partial")));
	TestTrue(TEXT("summary contains"), M.Summary.Contains(TEXT("alpha")) && M.Summary.Contains(TEXT("beta")));
	TestEqual(TEXT("errors"), M.Errors.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiPlanDagValidationTest,
	"UnrealAiEditor.Harness.PlanDagValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiPlanDagValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString ValidDag = TEXT(
		"{\"schema\":\"unreal_ai.plan_dag\",\"title\":\"t\",\"nodes\":["
		"{\"id\":\"n1\",\"title\":\"a\",\"depends_on\":[]},"
		"{\"id\":\"n2\",\"title\":\"b\",\"depends_on\":[\"n1\"]}"
		"]}");
	FUnrealAiPlanDag Dag;
	FString Err;
	TestTrue(TEXT("parse valid dag"), UnrealAiPlanDag::ParseDagJson(ValidDag, Dag, Err));
	TestTrue(TEXT("validate valid dag"), UnrealAiPlanDag::ValidateDag(Dag, 32, Err));

	const FString CyclicDag = TEXT(
		"{\"schema\":\"unreal_ai.plan_dag\",\"nodes\":["
		"{\"id\":\"a\",\"depends_on\":[\"b\"]},"
		"{\"id\":\"b\",\"depends_on\":[\"a\"]}"
		"]}");
	FUnrealAiPlanDag BadDag;
	TestTrue(TEXT("parse cyclic dag"), UnrealAiPlanDag::ParseDagJson(CyclicDag, BadDag, Err));
	TestFalse(TEXT("detect cycle"), UnrealAiPlanDag::ValidateDag(BadDag, 32, Err));

	const FString LegacyStepsDag = TEXT(
		"{\"schemaVersion\":4,\"definitionOfDone\":\"d\",\"steps\":["
		"{\"id\":\"s1\",\"title\":\"first\",\"detail\":\"hint1\",\"dependsOn\":[]},"
		"{\"id\":\"s2\",\"title\":\"second\",\"detail\":\"hint2\",\"dependsOn\":[\"s1\"]}"
		"]}");
	FUnrealAiPlanDag StepDag;
	TestTrue(TEXT("parse legacy steps dag"), UnrealAiPlanDag::ParseDagJson(LegacyStepsDag, StepDag, Err));
	TestTrue(TEXT("validate legacy steps dag"), UnrealAiPlanDag::ValidateDag(StepDag, 32, Err));
	TestEqual(TEXT("legacy node count"), StepDag.Nodes.Num(), 2);
	TestEqual(TEXT("legacy hint from detail"), StepDag.Nodes[1].Hint, FString(TEXT("hint2")));

	const FString DagWithChatNameSuffix = ValidDag + TEXT("\n<chat-name: \"Test\">");
	FUnrealAiPlanDag Suffixed;
	TestTrue(TEXT("parse dag with chat-name suffix"), UnrealAiPlanDag::ParseDagJson(DagWithChatNameSuffix, Suffixed, Err));
	TestEqual(TEXT("suffix node count"), Suffixed.Nodes.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiPlanReadySetTest,
	"UnrealAiEditor.Harness.PlanReadySet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiPlanReadySetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPlanDag Dag;
	Dag.Nodes = {
		{TEXT("n1"), TEXT("first"), TEXT(""), {}},
		{TEXT("n2"), TEXT("second"), TEXT(""), {TEXT("n1")}},
		{TEXT("n3"), TEXT("third"), TEXT(""), {TEXT("n1")}}};
	TMap<FString, FString> Status;
	TArray<FString> Ready;
	UnrealAiPlanDag::GetReadyNodeIds(Dag, Status, Ready);
	TestEqual(TEXT("only root ready"), Ready.Num(), 1);
	TestEqual(TEXT("root id"), Ready[0], FString(TEXT("n1")));
	Status.Add(TEXT("n1"), TEXT("success"));
	UnrealAiPlanDag::GetReadyNodeIds(Dag, Status, Ready);
	TestEqual(TEXT("children ready after root"), Ready.Num(), 2);
	return true;
}

#endif
