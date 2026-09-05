#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Widgets/UnrealAiAgentTypeDisplay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiAgentTypeRegistryCoverageTest,
	"UnrealAiEditor.Widgets.AgentTypeDisplay.RegistryCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiAgentTypeRegistryCoverageTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<FString> Keys = {
		UnrealAiAgentTypeDisplay::Keys::ModeAsk,
		UnrealAiAgentTypeDisplay::Keys::ModeAgent,
		UnrealAiAgentTypeDisplay::Keys::ModePlan,
		UnrealAiAgentTypeDisplay::Keys::ExecutionMainRun,
		UnrealAiAgentTypeDisplay::Keys::ExecutionPlannerPass,
		UnrealAiAgentTypeDisplay::Keys::ExecutionPlanWorker,
		UnrealAiAgentTypeDisplay::Keys::ExecutionBuilderSubturn,
		UnrealAiAgentTypeDisplay::Keys::ExecutionSpecialistSubturn,
		UnrealAiAgentTypeDisplay::Keys::BlueprintScript,
		UnrealAiAgentTypeDisplay::Keys::BlueprintAnim,
		UnrealAiAgentTypeDisplay::Keys::BlueprintMaterialInstance,
		UnrealAiAgentTypeDisplay::Keys::BlueprintMaterialGraph,
		UnrealAiAgentTypeDisplay::Keys::BlueprintNiagara,
		UnrealAiAgentTypeDisplay::Keys::BlueprintWidget,
		UnrealAiAgentTypeDisplay::Keys::SpecialistScene,
		UnrealAiAgentTypeDisplay::Keys::SpecialistAssets,
		UnrealAiAgentTypeDisplay::Keys::SpecialistViewport,
		UnrealAiAgentTypeDisplay::Keys::SpecialistDiagnostics,
		UnrealAiAgentTypeDisplay::Keys::SpecialistPlaytest,
		UnrealAiAgentTypeDisplay::Keys::SpecialistAnimation,
		UnrealAiAgentTypeDisplay::Keys::SpecialistProjectIntel,
		UnrealAiAgentTypeDisplay::Keys::SpecialistEditorUi,
		UnrealAiAgentTypeDisplay::Keys::SpecialistSettings,
		UnrealAiAgentTypeDisplay::Keys::SpecialistMaterials
	};

	for (const FString& Key : Keys)
	{
		const FUnrealAiAgentTypeDisplayInfo Info = UnrealAiAgentTypeDisplay::Resolve(Key);
		TestEqual(FString::Printf(TEXT("Resolve preserves key: %s"), *Key), Info.Key, Key);
		TestTrue(FString::Printf(TEXT("Label exists: %s"), *Key), !Info.Label.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiAgentTypeSpecialistMappingTest,
	"UnrealAiEditor.Widgets.AgentTypeDisplay.SpecialistNameMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiAgentTypeSpecialistMappingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(
		TEXT("Scene specialist mapping"),
		UnrealAiAgentTypeDisplay::SpecialistDisplayNameToKey(TEXT("Scene specialist")),
		FString(UnrealAiAgentTypeDisplay::Keys::SpecialistScene));
	TestEqual(
		TEXT("Materials specialist mapping"),
		UnrealAiAgentTypeDisplay::SpecialistDisplayNameToKey(TEXT("Materials specialist")),
		FString(UnrealAiAgentTypeDisplay::Keys::SpecialistMaterials));
	TestEqual(
		TEXT("Unknown specialist fallback mapping"),
		UnrealAiAgentTypeDisplay::SpecialistDisplayNameToKey(TEXT("Unknown specialist")),
		FString(UnrealAiAgentTypeDisplay::Keys::ExecutionSpecialistSubturn));
	return true;
}

#endif
