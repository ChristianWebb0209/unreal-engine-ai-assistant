#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Best-effort timestamps for harness stall analysis (game-thread sync timeout vs HTTP vs tools).
 * Safe to call from game thread and from HTTP completion callbacks; internally synchronized.
 */
namespace UnrealAiHarnessProgressTelemetry
{
	void ResetForRun();
	void NotifyAssistantDelta();
	void NotifyThinkingDelta();
	void NotifyToolFinish(const FString& ToolName);
	void NotifyLlmSubmit();
	/** Successful HTTP response received (before SSE parse). */
	void NotifyHttpResponseComplete();
	void NotifyPlanSubTurnComplete();

	TSharedPtr<FJsonObject> BuildHarnessSyncTimeoutDiagnosticJson(
		const FString& AgentModeLabel,
		uint32 SyncWaitMs,
		int32 PlanSubTurnCompletionsBeforeTimeout,
		bool bPlanMode);

	/** Human-readable line for Output Log (duplicates key JSON fields). */
	FString FormatHarnessSyncTimeoutLogLine(
		const FString& AgentModeLabel,
		uint32 SyncWaitMs,
		int32 PlanSubTurnCompletionsBeforeTimeout,
		bool bPlanMode);
}
