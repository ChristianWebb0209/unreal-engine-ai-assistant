#pragma once

#include "CoreMinimal.h"

/**
 * Wall-clock and harness wait budgets only (HTTP timeouts, game-thread sync, pacing, stream guards).
 * Kept separate from UnrealAiRuntimeDefaults.h so tuning timeouts does not mix with unrelated defaults.
 */
namespace UnrealAiWaitTime
{
	// --- Harness scenario runner (game-thread sync, one segment) ---
	/**
	 * Max time to wait for DoneEvent (or plan sub-turn) while pumping HTTP + game thread.
	 * Should exceed a single chat HTTP timeout plus at least one slow tool round, but need not be enormous.
	 */
	inline constexpr uint32 HarnessSyncWaitMs = 300000; // 5 min / segment

	/**
	 * Headed harness: if the turn is still non-terminal but HTTP is complete and stream telemetry is idle
	 * for this long (no active tool work), cancel early instead of waiting the full HarnessSyncWaitMs.
	 * 0 = disabled. Kept short so stuck planner/agent turns fail fast after tokens stop (see fine-tuning log).
	 */
	inline constexpr uint32 HarnessSyncIdleAbortMs = 3000;

	/**
	 * Headed scenario sync: when a plan pipeline is active (planner + serial node turns), post-tool / post-token
	 * gaps can exceed HarnessSyncIdleAbortMs without being a true stall. 0 = use HarnessSyncIdleAbortMs only.
	 */
	inline constexpr uint32 HarnessPlanPipelineSyncIdleAbortMs = 45000;

	/**
	 * If the HTTP layer reports the response complete (incl. SSE parse) but the runner never receives
	 * a Finish event, fail the turn after this much idle time on both HTTP and assistant telemetry.
	 * 0 = disabled. Complements headed sync idle abort (same order of magnitude).
	 */
	inline constexpr uint32 HarnessStreamNoFinishGraceMs = 5000;

	/** After CancelTurn(), drain window so sink/game-thread work can flush before forced terminal. */
	inline constexpr uint32 HarnessCancelDrainWaitMs = 5000;

	// --- Plan executor: 0 = no total wall-clock cap between segments ---
	inline constexpr uint32 HarnessPlanMaxWallMs = 0;

	/** Max LLM rounds for EUnrealAiAgentMode::Plan (planner DAG only); agent plan nodes keep profile limits. */
	inline constexpr int32 PlannerMaxLlmRounds = 16;

	/** Upper bound on LLM rounds for Agent turns whose thread id is a plan node (`*_plan_*`); applied in harness DispatchLlm. */
	inline constexpr int32 PlanNodeMaxLlmRounds = 12;

	// --- Streamed tool-call incomplete guard (SSE can split tool JSON across chunks) ---
	/** Run-20 step_01: `stream_tool_call_incomplete_timeout` at age_events=64 (age_ms=4) — provider fragmented many tiny SSE chunks before tool JSON closed. */
	inline constexpr int32 StreamToolIncompleteMaxEvents = 128;
	inline constexpr int32 StreamToolIncompleteMaxMs = 120000; // 2 min

	// --- Optional pacing between LLM submits (0 = off) ---
	inline constexpr int32 HarnessRoundMinDelayMs = 800;

	// --- OpenAI-compatible chat/completions HTTP ---
	/** Single request until response completes (streaming is one connection). Keep below HarnessSyncWaitMs to leave room for tool work in the same segment. */
	inline constexpr float HttpRequestTimeoutSec = 240.0f; // 4 min
	/** Plan-mode planner pass only (FUnrealAiLlmRequest override). Shorter than HttpRequestTimeoutSec so stuck planner HTTP fails faster than generic agent turns. */
	inline constexpr float PlannerHttpRequestTimeoutSec = 90.0f;
	inline constexpr int32 Http429MaxAttempts = 6;

	// --- Embeddings HTTP (retrieval path) ---
	inline constexpr float EmbeddingHttpTimeoutSec = 15.0f;
}
