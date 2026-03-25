#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"

namespace UnrealAiHarnessScenarioRunner
{
/**
 * Runs one harness turn (same code path as Agent Chat) and writes JSONL + optional context dumps.
 * Must be called from the game thread, or it will dispatch to the game thread and block until done.
 *
 * @param OutputRootDir If empty, uses Saved/UnrealAiEditor/HarnessRuns/<timestamp>/
 * @param OutJsonlPath Full path to run.jsonl
 * @param OutRunDir Directory containing run.jsonl and context dumps
 */
bool RunAgentTurnSync(
	const FString& UserMessage,
	const FString& ThreadIdDigitsWithHyphens,
	EUnrealAiAgentMode Mode,
	const FString& OutputRootDir,
	FString& OutJsonlPath,
	FString& OutRunDir,
	bool& bOutSuccess,
	FString& OutError,
	bool bDumpContextAfterEachTool = false);
}
