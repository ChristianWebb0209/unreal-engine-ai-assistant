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

	/** After CancelTurn(), drain window so sink/game-thread work can flush before forced terminal. */
	inline constexpr uint32 HarnessCancelDrainWaitMs = 5000;

	// --- Plan executor: 0 = no total wall-clock cap between segments ---
	inline constexpr uint32 HarnessPlanMaxWallMs = 0;

	/** Max LLM rounds for EUnrealAiAgentMode::Plan (planner DAG only); agent plan nodes keep profile limits. */
	inline constexpr int32 PlannerMaxLlmRounds = 16;

	// --- Streamed tool-call incomplete guard (SSE can split tool JSON across chunks) ---
	inline constexpr int32 StreamToolIncompleteMaxEvents = 64;
	inline constexpr int32 StreamToolIncompleteMaxMs = 120000; // 2 min

	// --- Optional pacing between LLM submits (0 = off) ---
	inline constexpr int32 HarnessRoundMinDelayMs = 800;

	// --- OpenAI-compatible chat/completions HTTP ---
	/** Single request until response completes (streaming is one connection). Keep below HarnessSyncWaitMs to leave room for tool work in the same segment. */
	inline constexpr float HttpRequestTimeoutSec = 240.0f; // 4 min
	inline constexpr int32 Http429MaxAttempts = 6;

	// --- Embeddings HTTP (retrieval path) ---
	inline constexpr float EmbeddingHttpTimeoutSec = 15.0f;
}
