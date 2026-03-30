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
	 * 0 = disabled. Run-29 step_03: post-tool assistant replies need more than 3s on slow models; 12s reduces false idle-abort before run_finished.
	 */
	inline constexpr uint32 HarnessSyncIdleAbortMs = 12000;

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

	/**
	 * Headed `UnrealAiHarnessScenarioRunner::RunAgentTurnSync` only: absolute wall clock for one sync invocation
	 * (planner + nodes or a single agent turn). Env `UNREAL_AI_HARNESS_SCENARIO_MAX_WALL_MS` overrides; 0 = disabled.
	 */
	inline constexpr uint32 HarnessScenarioMaxWallMs = 300000; // 5 min

	// --- Plan executor: wall clock cap between planner/node segments (complements scenario runner mid-turn deadline) ---
	inline constexpr uint32 HarnessPlanMaxWallMs = 300000; // 5 min; 0 would disable

	/**
	 * Plan-mode harness: max `harness_sync_segment_wait_start` iterations without `run_finished` (run-64 style loops).
	 * Stops endless sub_turn resets.
	 */
	inline constexpr int32 HarnessPlanMaxSyncSegments = 56;

	/**
	 * Game-thread pump window for a synchronous plan replan planner `RunTurn` (headed wall-stall path).
	 * Should stay below typical HTTP client limits; the replan prompt is compact.
	 */
	inline constexpr uint32 HarnessPlanReplanSyncMaxMs = 120000;

	/**
	 * Headed scenario strict tool budgets only (`SetHeadedScenarioStrictToolBudgets`): max `editor_state_snapshot_read`
	 * invocations per RunTurn; further calls are blocked (fourth attempt fails).
	 */
	inline constexpr int32 HarnessScenarioMaxReadSnapshotToolsPerTurn = 3;

	/**
	 * Max nodes in a DAG emitted by the planner model (FUnrealAiPlanExecutor::OnPlannerFinished).
	 * User-edited / Build / Resume-from-JSON paths keep a separate higher cap (64) so manual large graphs still validate.
	 */
	inline constexpr int32 PlannerEmittedMaxDagNodes = 8;

	/** Max LLM rounds for EUnrealAiAgentMode::Plan (planner DAG only); agent plan nodes keep profile limits. */
	inline constexpr int32 PlannerMaxLlmRounds = 16;

	/** Upper bound on LLM rounds for Agent turns whose thread id is a plan node (`*_plan_*`); applied in harness DispatchLlm. */
	inline constexpr int32 PlanNodeMaxLlmRounds = 8;

	/**
	 * Plan-node agent threads (`*_plan_*`): max invocations of the same inner tool name per node before harness blocks
	 * further calls (the next attempt is rejected; the turn fails so the DAG executor can cascade/skip dependents).
	 * Example: 4 allows four real invocations; the fifth is blocked without running the tool.
	 * Applied to tools that do not match the read-only name heuristic in FUnrealAiAgentHarness (`IsLikelyReadOnlyToolName`).
	 */
	inline constexpr int32 PlanNodeMaxInvocationsSameTool = 4;

	/**
	 * Same as PlanNodeMaxInvocationsSameTool for discovery/read-style tools (`IsLikelyReadOnlyToolName`):
	 * fuzzy search, snapshot read, *_get_*, etc., so plan nodes can issue several bounded searches without tripping the cap.
	 */
	inline constexpr int32 PlanNodeMaxInvocationsSameToolReadOnly = 10;

	/**
	 * Plan-node agent threads only: after HTTP completes without a stream Finish event, re-issue the same LLM round
	 * (DispatchLlm(true)) at most this many times before failing the node. 0 = never retry (legacy behavior).
	 */
	inline constexpr int32 PlanNodeStreamNoFinishMaxRetries = 1;

	/**
	 * Plan-node agent threads: override for HarnessStreamNoFinishGraceMs when > 0; 0 = use HarnessStreamNoFinishGraceMs.
	 */
	inline constexpr uint32 PlanNodeStreamNoFinishGraceMs = 0;

	/**
	 * Plan-node agent threads: max automatic DispatchLlm(true) retries per LLM round when the stream Error matches
	 * ShouldRetryTransientTransportError (timeouts, cancellation). Generic Agent turns keep 0.
	 */
	inline constexpr int32 PlanNodeTransientHttpMaxRetries = 1;

	/**
	 * Plan-node agent threads: fail fast when the same validation-style tool failure signature repeats this many times.
	 * Keeps nodes from burning rounds on identical "missing required args" loops.
	 */
	inline constexpr int32 PlanNodeRepeatedValidationFailFastThreshold = 2;

	// --- Streamed tool-call incomplete guard (SSE can split tool JSON across chunks) ---
	/** Run-20 step_01: `stream_tool_call_incomplete_timeout` at age_events=64 (age_ms=4) — provider fragmented many tiny SSE chunks before tool JSON closed. */
	inline constexpr int32 StreamToolIncompleteMaxEvents = 128;
	inline constexpr int32 StreamToolIncompleteMaxMs = 120000; // 2 min

	/**
	 * `FAgentTurnRunner::TryParseArgumentsJsonComplete`: refuse to call `FJsonSerializer::Deserialize` when the
	 * streamed `arguments` string is larger than this. Oversize / malformed tool JSON has triggered UE JsonReader
	 * BufferReader asserts instead of a clean parse failure; treat as "not yet complete" for readiness.
	 */
	inline constexpr int32 MaxToolArgumentsJsonDeserializeChars = 2 * 1024 * 1024;

	// --- Optional pacing between LLM submits (0 = off) ---
	inline constexpr int32 HarnessRoundMinDelayMs = 0;

	// --- OpenAI-compatible chat/completions HTTP ---
	/** Single request until response completes (streaming is one connection). Keep below HarnessSyncWaitMs to leave room for tool work in the same segment. */
	inline constexpr float HttpRequestTimeoutSec = 240.0f; // 4 min
	/** Plan-mode planner pass only (FUnrealAiLlmRequest override). Shorter than HttpRequestTimeoutSec so stuck planner HTTP fails faster than generic agent turns. */
	inline constexpr float PlannerHttpRequestTimeoutSec = 90.0f;
	inline constexpr int32 Http429MaxAttempts = 6;

	// --- Embeddings HTTP (retrieval path) ---
	// Keep embeddings fail-fast so retrieval falls back to lexical quickly instead of stalling a harness turn.
	inline constexpr float EmbeddingHttpTimeoutSec = 8.0f;
}
