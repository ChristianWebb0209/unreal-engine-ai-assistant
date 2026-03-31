#include "Harness/UnrealAiHarnessTurnPaths.h"

#include "Misc/Paths.h"

static FString GCurrentStepOutputDir;

void FUnrealAiHarnessTurnPaths::SetCurrentStepOutputDir(const FString& AbsoluteDir)
{
	GCurrentStepOutputDir = FPaths::ConvertRelativePathToFull(AbsoluteDir);
}

void FUnrealAiHarnessTurnPaths::ClearCurrentStepOutputDir()
{
	GCurrentStepOutputDir.Reset();
}

const FString& FUnrealAiHarnessTurnPaths::GetCurrentStepOutputDir()
{
	return GCurrentStepOutputDir;
}

bool FUnrealAiHarnessTurnPaths::HasCurrentStepOutputDir()
{
	return !GCurrentStepOutputDir.IsEmpty();
}

FScopedHarnessStepOutputDir::FScopedHarnessStepOutputDir(const FString& AbsoluteDir)
{
	FUnrealAiHarnessTurnPaths::SetCurrentStepOutputDir(AbsoluteDir);
}

FScopedHarnessStepOutputDir::~FScopedHarnessStepOutputDir()
{
	FUnrealAiHarnessTurnPaths::ClearCurrentStepOutputDir();
}
