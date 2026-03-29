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
	/** After full SSE body parsed (same callback stack as NotifyHttpResponseComplete for non-stream). */
	void NotifyHttpStreamParseComplete();
	void NotifyPlanSubTurnComplete();

	/** Last reason TryHarnessIdleAbort returned false (best-effort; for timeout diagnostics). */
	void SetIdleAbortSkipReason(const TCHAR* Reason);
	FString GetIdleAbortSkipReason();

	/** Age in seconds since last assistant delta / HTTP complete / LLM submit; -1 if that event never occurred this run. */
	void GetStreamIdleSeconds(double& OutAssistantIdleSec, double& OutHttpIdleSec, double& OutLlmSubmitIdleSec);

	TSharedPtr<FJsonObject> BuildHarnessSyncIdleAbortDiagnosticJson(
		const FString& AgentModeLabel,
		uint32 IdleAbortMs,
		bool bPlanMode,
		bool bHasActiveLlmTransport,
		bool bSuppressIdleAbort);

	FString FormatHarnessSyncIdleAbortLogLine(
		const FString& AgentModeLabel,
		uint32 IdleAbortMs,
		bool bPlanMode,
		bool bHasActiveLlmTransport,
		bool bSuppressIdleAbort);

	TSharedPtr<FJsonObject> BuildHarnessSyncTimeoutDiagnosticJson(
		const FString& AgentModeLabel,
		uint32 SyncWaitMs,
		int32 PlanSubTurnCompletionsBeforeTimeout,
		bool bPlanMode,
		const FString& IdleAbortSkipReason = FString());

	/** Human-readable line for Output Log (duplicates key JSON fields). */
	FString FormatHarnessSyncTimeoutLogLine(
		const FString& AgentModeLabel,
		uint32 SyncWaitMs,
		int32 PlanSubTurnCompletionsBeforeTimeout,
		bool bPlanMode);
}
