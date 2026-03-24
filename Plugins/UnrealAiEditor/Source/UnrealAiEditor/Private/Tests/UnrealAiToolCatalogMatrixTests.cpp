#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"

#include "Tools/UnrealAiToolCatalogMatrixRunner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiToolCatalogMatrixTest,
	"UnrealAiEditor.Tools.CatalogMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiToolCatalogMatrixTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FString MatrixFilter;
	FParse::Value(FCommandLine::Get(), TEXT("-UnrealAiToolMatrixFilter="), MatrixFilter);

	TArray<FString> Violations;
	const bool bOk = UnrealAiToolCatalogMatrixRunner::RunAndWriteJson(MatrixFilter, &Violations);
	for (const FString& Line : Violations)
	{
		AddError(Line);
	}
	return bOk;
}

#endif // WITH_DEV_AUTOMATION_TESTS
