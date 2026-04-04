#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/UnrealAiHarnessProgressTelemetry.h"
#include "Planning/UnrealAiPlanDag.h"
#include "Harness/UnrealAiAgentTypes.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiPlanTransitiveDependentsTest,
	"UnrealAiEditor.Harness.PlanTransitiveDependents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiPlanTransitiveDependentsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// n1 -> n2 -> n4; n1 -> n3; independent chain n5 -> n6
	FUnrealAiPlanDag Dag;
	Dag.Nodes = {
		{TEXT("n1"), TEXT(""), TEXT(""), {}},
		{TEXT("n2"), TEXT(""), TEXT(""), {TEXT("n1")}},
		{TEXT("n3"), TEXT(""), TEXT(""), {TEXT("n1")}},
		{TEXT("n4"), TEXT(""), TEXT(""), {TEXT("n2")}},
		{TEXT("n5"), TEXT(""), TEXT(""), {}},
		{TEXT("n6"), TEXT(""), TEXT(""), {TEXT("n5")}},
	};
	TArray<FString> Out;
	UnrealAiPlanDag::CollectTransitiveDependents(Dag, TEXT("n1"), Out);
	TestEqual(TEXT("n1 transitive count"), Out.Num(), 3);
	TestTrue(TEXT("n2 in transitive"), Out.Contains(TEXT("n2")));
	TestTrue(TEXT("n3 in transitive"), Out.Contains(TEXT("n3")));
	TestTrue(TEXT("n4 in transitive"), Out.Contains(TEXT("n4")));
	TestFalse(TEXT("n5 not in transitive"), Out.Contains(TEXT("n5")));
	TestFalse(TEXT("n6 not in transitive"), Out.Contains(TEXT("n6")));

	Out.Reset();
	UnrealAiPlanDag::CollectTransitiveDependents(Dag, TEXT("n5"), Out);
	TestEqual(TEXT("n5 transitive count"), Out.Num(), 1);
	TestTrue(TEXT("n6 under n5"), Out.Contains(TEXT("n6")));

	Out.Reset();
	UnrealAiPlanDag::CollectTransitiveDependents(Dag, TEXT(""), Out);
	TestEqual(TEXT("empty failed id"), Out.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiHarnessProgressTelemetryCombinedIdleTest,
	"UnrealAiEditor.Tools.HarnessProgressTelemetryCombinedIdle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiHarnessProgressTelemetryCombinedIdleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealAiHarnessProgressTelemetry::ResetForRun();
	UnrealAiHarnessProgressTelemetry::NotifyThinkingDelta();
	double RawAsst = 0.0, Http = 0.0, Llm = 0.0;
	UnrealAiHarnessProgressTelemetry::GetStreamIdleSeconds(RawAsst, Http, Llm);
	TestTrue(TEXT("assistant-only idle unset before assistant delta"), RawAsst < 0.0);
	double Combined = -1.0;
	UnrealAiHarnessProgressTelemetry::GetAssistantOrThinkingIdleSeconds(Combined);
	TestTrue(TEXT("thinking-only updates combined stream idle"), Combined >= 0.0 && Combined < 5.0);

	UnrealAiHarnessProgressTelemetry::NotifyAssistantDelta();
	UnrealAiHarnessProgressTelemetry::GetAssistantOrThinkingIdleSeconds(Combined);
	TestTrue(TEXT("assistant delta also yields fresh combined idle"), Combined >= 0.0 && Combined < 5.0);
	return true;
}

#endif
