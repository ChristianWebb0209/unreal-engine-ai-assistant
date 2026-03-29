#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tools/UnrealAiToolKPolicy.h"
#include "Tools/UnrealAiToolQueryShaper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiToolKPolicyTest,
	"UnrealAiEditor.Tools.KPolicyMargin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUnrealAiToolKPolicyTest::RunTest(const FString& Parameters)
{
	{
		TArray<float> Sharp{1.f, 0.2f, 0.1f, 0.05f};
		const int32 K = UnrealAiToolKPolicy::ComputeEffectiveK(Sharp, 4, 20);
		TestEqual(TEXT("sharp margin -> KMin"), K, 4);
	}
	{
		TArray<float> Amb;
		for (int32 I = 0; I < 24; ++I)
		{
			Amb.Add(1.0f - static_cast<float>(I) * 0.001f);
		}
		const int32 K = UnrealAiToolKPolicy::ComputeEffectiveK(Amb, 4, 20);
		TestTrue(TEXT("ambiguous margin -> larger K"), K >= 10);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiToolQueryShaperTest,
	"UnrealAiEditor.Tools.QueryShaperHeuristic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUnrealAiToolQueryShaperTest::RunTest(const FString& Parameters)
{
	FString Shaped;
	EUnrealAiToolQueryShape Shape = EUnrealAiToolQueryShape::Raw;
	UnrealAiToolQueryShaper::ShapeForRetrieval(TEXT("Please create a new Blueprint for the character"), Shaped, Shape);
	TestEqual(TEXT("heuristic shape"), static_cast<int32>(Shape), static_cast<int32>(EUnrealAiToolQueryShape::Heuristic));
	TestTrue(TEXT("shaped mentions blueprint"), Shaped.Contains(TEXT("blueprint"), ESearchCase::IgnoreCase));

	UnrealAiToolQueryShaper::ShapeForRetrieval(
		TEXT("Use the editor world collision / line trace tool from the viewport camera"), Shaped, Shape);
	TestEqual(TEXT("trace prompt uses physics_trace heuristic"), static_cast<int32>(Shape),
		static_cast<int32>(EUnrealAiToolQueryShape::Heuristic));
	TestTrue(TEXT("shaped mentions physics_trace"), Shaped.Contains(TEXT("physics_trace")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
