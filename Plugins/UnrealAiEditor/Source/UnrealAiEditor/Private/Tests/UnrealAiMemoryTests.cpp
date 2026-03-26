#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Backend/FUnrealAiPersistenceStub.h"
#include "Context/UnrealAiContextCandidates.h"
#include "Memory/FUnrealAiMemoryCompactor.h"
#include "Memory/FUnrealAiMemoryService.h"
#include "Memory/UnrealAiMemoryJson.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiMemoryJsonRoundTripTest,
	"UnrealAiEditor.Memory.Json.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiMemoryJsonRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiMemoryRecord In;
	In.Id = TEXT("memory-json-test");
	In.Title = TEXT("Blueprint naming guideline");
	In.Description = TEXT("Use readable event names.");
	In.Body = TEXT("Always keep event graph names clear and stable.");
	In.Tags = { TEXT("blueprint"), TEXT("workflow") };
	In.Confidence = 0.8f;
	In.TtlDays = 30;

	FString Json;
	TestTrue(TEXT("RecordToJson"), UnrealAiMemoryJson::RecordToJson(In, Json));
	FUnrealAiMemoryRecord Out;
	TArray<FString> Warnings;
	TestTrue(TEXT("JsonToRecord"), UnrealAiMemoryJson::JsonToRecord(Json, Out, Warnings));
	TestEqual(TEXT("Id preserved"), Out.Id, In.Id);
	TestEqual(TEXT("Title preserved"), Out.Title, In.Title);
	TestEqual(TEXT("Tags preserved"), Out.Tags.Num(), In.Tags.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiMemoryServiceCrudTest,
	"UnrealAiEditor.Memory.Service.CrudAndQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiMemoryServiceCrudTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPersistenceStub Persist;
	FUnrealAiMemoryService Service(&Persist);
	Service.Load();

	FUnrealAiMemoryRecord Record;
	Record.Id = TEXT("memory-service-test");
	Record.Title = TEXT("Material workflow lesson");
	Record.Description = TEXT("Set scalar params through instances.");
	Record.Body = TEXT("Prefer material instances for scalar tweaks.");
	Record.Tags = { TEXT("materials"), TEXT("workflow") };
	Record.Confidence = 0.9f;
	TestTrue(TEXT("UpsertMemory"), Service.UpsertMemory(Record));

	FUnrealAiMemoryRecord ReadBack;
	TestTrue(TEXT("GetMemory"), Service.GetMemory(Record.Id, ReadBack));
	TestEqual(TEXT("Read title"), ReadBack.Title, Record.Title);

	FUnrealAiMemoryQuery Query;
	Query.QueryText = TEXT("material");
	Query.MaxResults = 5;
	Query.MinConfidence = 0.5f;
	TArray<FUnrealAiMemoryQueryResult> Results;
	Service.QueryMemories(Query, Results);
	TestTrue(TEXT("Query returns at least one row"), Results.Num() >= 1);

	TestTrue(TEXT("DeleteMemory"), Service.DeleteMemory(Record.Id));
	FUnrealAiMemoryRecord Missing;
	TestFalse(TEXT("Deleted memory not loadable"), Service.GetMemory(Record.Id, Missing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiMemoryCompactorGateTest,
	"UnrealAiEditor.Memory.Compactor.Gates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiMemoryCompactorGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPersistenceStub Persist;
	FUnrealAiMemoryService Service(&Persist);
	Service.Load();
	FUnrealAiMemoryCompactor Compactor(&Service);

	FUnrealAiMemoryCompactionInput LowInput;
	LowInput.ThreadId = TEXT("thread-low");
	LowInput.ConversationJson = TEXT("short");
	const FUnrealAiMemoryCompactionResult Low = Compactor.Run(LowInput, 1, 0.55f);
	TestEqual(TEXT("Low confidence creates zero"), Low.Accepted, 0);

	FUnrealAiMemoryCompactionInput HighInput;
	HighInput.ThreadId = TEXT("thread-high");
	HighInput.ConversationJson = FString::ChrN(400, TEXT('a'));
	const FUnrealAiMemoryCompactionResult High = Compactor.Run(HighInput, 1, 0.55f);
	TestEqual(TEXT("High confidence creates one"), High.Accepted, 1);
	if (High.AcceptedMemoryIds.Num() > 0)
	{
		Service.DeleteMemory(High.AcceptedMemoryIds[0]);
	}

	FUnrealAiMemoryCompactionInput MissingKeyInput;
	MissingKeyInput.ThreadId = TEXT("thread-missing-key");
	MissingKeyInput.ConversationJson = FString::ChrN(400, TEXT('b'));
	MissingKeyInput.bExpectProviderGeneration = true;
	MissingKeyInput.bApiKeyConfigured = false;
	const FUnrealAiMemoryCompactionResult MissingKey = Compactor.Run(MissingKeyInput, 1, 0.55f);
	TestEqual(TEXT("Missing key generates zero"), MissingKey.Accepted, 0);
	const FUnrealAiMemoryGenerationStatus S = Service.GetGenerationStatus();
	TestEqual(TEXT("Missing key sets failed state"), static_cast<int32>(S.State), static_cast<int32>(EUnrealAiMemoryGenerationState::Failed));
	TestEqual(TEXT("Missing key error code"), static_cast<int32>(S.ErrorCode), static_cast<int32>(EUnrealAiMemoryGenerationErrorCode::MissingApiKey));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiMemoryRankerCandidateTest,
	"UnrealAiEditor.Memory.Ranker.Candidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiMemoryRankerCandidateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FUnrealAiPersistenceStub Persist;
	FUnrealAiMemoryService MemoryService(&Persist);
	MemoryService.Load();

	FUnrealAiMemoryRecord Record;
	Record.Id = TEXT("memory-ranker-test");
	Record.Title = TEXT("Inventory panel workflow");
	Record.Description = TEXT("Details panel and widget selection for inventory.");
	Record.Body = TEXT("Full inventory memory body.");
	Record.Tags = { TEXT("inventory"), TEXT("ui") };
	Record.Confidence = 0.9f;
	TestTrue(TEXT("Upsert memory for ranker"), MemoryService.UpsertMemory(Record));

	FAgentContextState State;
	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	Opt.UserMessageForComplexity = TEXT("Update inventory panel and details selection.");
	Opt.MaxContextChars = 1200;
	const UnrealAiContextCandidates::FUnifiedContextBuildResult R =
		UnrealAiContextCandidates::BuildUnifiedContext(State, Opt, &MemoryService, Opt.MaxContextChars);
	TestTrue(TEXT("Context includes memory"), R.ContextBlock.Contains(TEXT("Memory: Inventory panel workflow")));

	MemoryService.DeleteMemory(Record.Id);
	return true;
}

#endif
