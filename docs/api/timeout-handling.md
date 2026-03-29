# Timeout and wall-clock limits — inventory and analysis

This document lists **every distinct mechanism** in Unreal AI Editor that uses timeouts, waits, or bounded work. It is the reference for post-mortems when runs feel “stuck” after raising limits.

**Single source of numeric defaults:** [`Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Misc/UnrealAiRuntimeDefaults.h`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Misc/UnrealAiRuntimeDefaults.h).

---

## 1. Inventory (what exists today)

| Mechanism | Purpose | Typical constant / behavior | Where |
|-----------|---------|------------------------------|--------|
| **Chat / completions HTTP** | Wall-clock cap on a **single** OpenAI-compatible request (streaming uses one connection until completion). | `HttpRequestTimeoutSec` (e.g. 1200s) | `FOpenAiCompatibleHttpTransport.cpp` |
| **HTTP 429 retries** | Back off and retry when the provider returns rate limits. | `Http429MaxAttempts`; waits from `Retry-After` / response body or exponential fallback | Same transport |
| **Embeddings HTTP** | Fail-fast for small embedding calls (retrieval path). | `EmbeddingHttpTimeoutSec` (e.g. 3s) | `FOpenAiCompatibleEmbeddingProvider.cpp` |
| **Harness game-thread sync** | While `UnrealAi.RunAgentTurn` runs, the scenario runner **blocks the game thread** but **pumps** HTTP + game-thread tasks until `DoneEvent` fires or the wait elapses. | `HarnessSyncWaitMs` (e.g. 1500000 ms ≈ 25 min **per segment**) | `UnrealAiHarnessScenarioRunner.cpp` |
| **Plan mode segments** | Plan runs **planner** then **per-node** agent work. Each **sub-turn** resets the sync window; **overall** wall across segments is optional. | Per segment: same `HarnessSyncWaitMs`; total plan wall: `HarnessPlanMaxWallMs` (0 = off) | `UnrealAiHarnessScenarioRunner.cpp`, `Private/Planning/FUnrealAiPlanExecutor` |
| **Cancel drain** | After sync timeout, `CancelTurn()` runs, then the runner waits up to **3s** for a terminal `run_finished`. | `CancelDrainWaitMs = 3000` (hardcoded in runner) | `UnrealAiHarnessScenarioRunner.cpp` |
| **Streamed tool-call incomplete** | SSE can split `tool_calls` across chunks; guard against “forever partial” JSON. | `StreamToolIncompleteMaxEvents`, `StreamToolIncompleteMaxMs` | `FUnrealAiAgentHarness.cpp` |
| **Inter–LLM-round pacing** | Optional delay between HTTP submits (429 / burst smoothing). | `HarnessRoundMinDelayMs` | `FUnrealAiAgentHarness.cpp` |
| **Max LLM rounds per user send** | Backstop against infinite tool/LLM loops. | Profile `maxAgentLlmRounds` or internal backstop (e.g. 512) | `FUnrealAiAgentHarness.cpp` |
| **Repeated identical OK / fail / empty search** | Stop earlier than max rounds when the model repeats non-progress patterns. | Enforcement events + terminal `error_message` | `FUnrealAiAgentHarness.cpp` |
| **TPM throttle (harness)** | Sliding-window token admission before chat/embed requests. | `HarnessTpmPerMinute`, `HarnessTpmWindowSec`, etc. | `UnrealAiHarnessTpmThrottle.cpp` |
| **Tool-internal wall clocks** | Search / scan tools stop with partial results and may set `timeout_notice` / `timed_out` in JSON. | Per-tool `max_wall_ms` caps (e.g. 120s) | e.g. `UnrealAiToolDispatch_Search.cpp` |
| **Retrieval service** | Comment-level concern: avoid burning **HTTP** budget in a tight loop. | Logic in retrieval service | `FUnrealAiRetrievalService.cpp` |
| **SQLite busy** | Vector index store busy timeout. | `PRAGMA busy_timeout=5000` | `UnrealAiVectorIndexStore.cpp` |
| **Console: WaitForReady** | `UnrealAi.Retrieval.WaitForReady [TimeoutSec]` | Default 120s if omitted | `UnrealAiEditorModule.cpp` |

**Not the same problem:** Raising `HttpRequestTimeoutSec` helps **slow LLM responses**; raising `HarnessSyncWaitMs` helps **long multi-tool turns**. Raising both does **not** fix **invalid API message ordering** (HTTP 400) or **harness loop aborts** (repeated identical tools).

---

## 2. Why “the right timeout” is unknowable without telemetry

- **Slow LLM** vs **stuck game thread** vs **dead HTTP** all look like “nothing in the log for a while.”
- **Plan mode** can spend time in **planner** vs **many node segments**; one giant `HarnessSyncWaitMs` masks **which** segment stalled.
- **429** waits are **intentionally** long when the provider asks for them — that is not a bug, but it extends wall time.
- **Classifier scripts** (`tests/classify_harness_run_jsonl.py`) bucket `Harness run timed out waiting for completion` under **`harness_policy`**, not `http_timeout`, because it is **runner cancel**, not transport `timed out` strings.

---

## 3. Latest batch analyzed: `run-4-20260328-165912_266`

**Source:** `tests/long-running-tests/last-suite-summary.json` → `batch_output_folder` = `tests/long-running-tests/runs/run-4-20260328-165912_266`.

**Suite:** `mixed-salad-complex` — 11 steps (ask + agent + **plan** + agent + ask).

**Aggregate (from `harness-classification.json`):**

| Outcome | Count | Notes |
|--------|-------|--------|
| `run_finished` success | 7 | — |
| `run_finished` failure | 4 | Mix of causes below |
| `tool_finish` success=false | 1 | Tool-level failure (not necessarily timeout) |

### 3.1 Step-by-step (failure modes)

| Step | Failure? | What actually happened | “Long timeout” related? |
|------|----------|-------------------------|-------------------------|
| **02** | Yes | **`repeated_identical_ok_abort`**: four consecutive identical successful `viewport_frame_actors` calls → harness stopped with a **policy** error (not wall-clock timeout). | **No** — this is **useful work that became useless repetition**; earlier abort would have saved time. |
| **07** (plan) | Yes | **`Harness run timed out waiting for completion (forced terminal after cancel)`** — only `run_started` + `run_finished` in `run.jsonl`; runner hit **`HarnessSyncWaitMs`** (or never got `DoneEvent` within a plan segment), called `CancelTurn()`, then forced terminal. | **Yes** — this is the **raised sync wait** path. Whether the plan was **still doing useful work** cannot be read from this minimal JSONL; check **editor log** around `sync_wait_timeout` and plan executor logs. |
| **08** | Yes | **HTTP 400** — `messages.[35].role` tool without preceding `tool_calls` (API validation). | **No** — **message-trim / ordering** bug or race, not HTTP socket timeout. |
| **09** | Yes | Same **HTTP 400** pattern at `messages.[33].role`. | **No** |

**Successful steps** included multi-tool agent work without hitting sync or loop aborts.

### 3.2 Interpretation

- **Raising global timeouts did not cause** the step 08/09 failures — those are **request-shape** errors.
- **Step 02** shows the opposite problem: **work continued until a harness policy fired**; a **shorter** identical-OK window or **stronger prompt** might have reduced wasted rounds **without** needing a 25-minute HTTP/sync budget.
- **Step 07** is the only clear **“we waited until the harness killed the turn”** case in this batch. To know if that was **legitimate long plan work** vs **hang**, you need **segment timestamps** (planner vs node) and/or **whether `sync_wait_timeout` appeared in the editor log**.

---

## 4. Making the system more “accurate” (actionable directions)

1. **Emit structured timeout reasons in `run.jsonl`**  
   On sync timeout: log **`timeout_class=sync_wait`**, **`wait_ms`**, **`mode=plan|agent|ask`**, and for plan **`plan_segment=planner|node_index`**. Today the file often only shows a generic `run_finished` error.

2. **Split budgets conceptually**  
   Keep **HTTP** timeout moderate for “single request hung”; use **separate** **per-plan-segment** or **per-LLM-round** sync budgets so one bad segment does not require a single huge `HarnessSyncWaitMs` for everything.

3. **Surface stall vs slow-LLM in logs**  
   Log **last HTTP activity time** vs **last tool_finish time** when canceling — distinguishes **waiting on provider** vs **stuck after tools**.

4. **Treat HTTP 400 tool-order as first-class**  
   Continue hardening `TrimApiMessagesForContextBudget` / ordering; classify these separately from timeouts in dashboards (already partially true in `classify_harness_run_jsonl.py`).

5. **Tune loop stops before wall-clock**  
   `repeated_identical_ok_abort` is doing the right economic thing; tuning **when** to stop (and prompting the model to change tool) reduces “forever” runs without increasing sync wait.

6. **Optional: adaptive `HarnessRoundMinDelayMs`**  
   If the goal is faster suites, **0** removes artificial delay; if the goal is fewer 429s, keep delay — document the trade-off in harness README.

7. **Re-run classification after each batch**  
   `python tests/classify_harness_run_jsonl.py --from-summary tests/long-running-tests/last-suite-summary.json` and archive `harness-classification.json` — separates **harness_policy** (sync cancel, loops) from **invalid_request** from **http_timeout**.

---

## 5. Related docs

- [`context.md`](../../context.md) — operational notes on HTTP vs sync vs trimming (repo root).
- [`AGENT_HARNESS_HANDOFF.md`](../tooling/AGENT_HARNESS_HANDOFF.md) — harness tiers and artifacts.
- [`tests/classify_harness_run_jsonl.py`](../../tests/classify_harness_run_jsonl.py) — error bucketing rules.

---

*When defaults change, update `UnrealAiRuntimeDefaults.h` and this document’s table (section 1).*
