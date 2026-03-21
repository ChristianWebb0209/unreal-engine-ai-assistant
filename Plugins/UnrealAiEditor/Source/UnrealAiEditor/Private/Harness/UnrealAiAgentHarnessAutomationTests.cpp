#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Harness/FUnrealAiWorkerOrchestrator.h"
#include "Harness/UnrealAiAgentTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiMergeDeterministicTest,
	"UnrealAiEditor.Harness.MergeDeterministic",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

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

#endif
