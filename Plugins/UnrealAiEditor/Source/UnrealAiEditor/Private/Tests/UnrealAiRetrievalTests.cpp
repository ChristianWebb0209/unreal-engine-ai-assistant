#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Backend/FUnrealAiPersistenceStub.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Retrieval/FUnrealAiRetrievalService.h"

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

#endif
