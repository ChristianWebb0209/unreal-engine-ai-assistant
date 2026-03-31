#pragma once

#include "CoreMinimal.h"

/**
 * During RunAgentTurnSync, the headed harness sets the absolute directory that holds run.jsonl (the per-turn step folder).
 * Project file tools may use relative_path values prefixed with harness_step/ to read/write beside that harness output.
 */
class FUnrealAiHarnessTurnPaths
{
public:
	static void SetCurrentStepOutputDir(const FString& AbsoluteDir);
	static void ClearCurrentStepOutputDir();
	static const FString& GetCurrentStepOutputDir();
	static bool HasCurrentStepOutputDir();
};

/** Pushes the current harness step directory for the lifetime of a headed RunAgentTurnSync call. */
class FScopedHarnessStepOutputDir
{
public:
	explicit FScopedHarnessStepOutputDir(const FString& AbsoluteDir);
	~FScopedHarnessStepOutputDir();
	FScopedHarnessStepOutputDir(const FScopedHarnessStepOutputDir&) = delete;
	FScopedHarnessStepOutputDir& operator=(const FScopedHarnessStepOutputDir&) = delete;
};
