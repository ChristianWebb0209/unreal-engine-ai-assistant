#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Harness/FUnrealAiWorkerOrchestrator.h"
#include "Harness/UnrealAiOrchestrateDag.h"
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
	FUnrealAiOrchestrateDagValidationTest,
	"UnrealAiEditor.Harness.OrchestrateDagValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiOrchestrateDagValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString ValidDag = TEXT(
		"{\"schema\":\"unreal_ai.orchestrate_dag\",\"title\":\"t\",\"nodes\":["
		"{\"id\":\"n1\",\"title\":\"a\",\"depends_on\":[]},"
		"{\"id\":\"n2\",\"title\":\"b\",\"depends_on\":[\"n1\"]}"
		"]}");
	FUnrealAiOrchestrateDag Dag;
	FString Err;
	TestTrue(TEXT("parse valid dag"), UnrealAiOrchestrateDag::ParseDagJson(ValidDag, Dag, Err));
	TestTrue(TEXT("validate valid dag"), UnrealAiOrchestrateDag::ValidateDag(Dag, 32, Err));

	const FString CyclicDag = TEXT(
		"{\"schema\":\"unreal_ai.orchestrate_dag\",\"nodes\":["
		"{\"id\":\"a\",\"depends_on\":[\"b\"]},"
		"{\"id\":\"b\",\"depends_on\":[\"a\"]}"
		"]}");
	FUnrealAiOrchestrateDag BadDag;
	TestTrue(TEXT("parse cyclic dag"), UnrealAiOrchestrateDag::ParseDagJson(CyclicDag, BadDag, Err));
	TestFalse(TEXT("detect cycle"), UnrealAiOrchestrateDag::ValidateDag(BadDag, 32, Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiOrchestrateReadySetTest,
	"UnrealAiEditor.Harness.OrchestrateReadySet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiOrchestrateReadySetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiOrchestrateDag Dag;
	Dag.Nodes = {
		{TEXT("n1"), TEXT("first"), TEXT(""), {}},
		{TEXT("n2"), TEXT("second"), TEXT(""), {TEXT("n1")}},
		{TEXT("n3"), TEXT("third"), TEXT(""), {TEXT("n1")}}};
	TMap<FString, FString> Status;
	TArray<FString> Ready;
	UnrealAiOrchestrateDag::GetReadyNodeIds(Dag, Status, Ready);
	TestEqual(TEXT("only root ready"), Ready.Num(), 1);
	TestEqual(TEXT("root id"), Ready[0], FString(TEXT("n1")));
	Status.Add(TEXT("n1"), TEXT("success"));
	UnrealAiOrchestrateDag::GetReadyNodeIds(Dag, Status, Ready);
	TestEqual(TEXT("children ready after root"), Ready.Num(), 2);
	return true;
}

#endif
