# Fine-tuning log

Chronicle of changes aimed at headed harness quality and API reliability. **Entries are freeform** (date heading + bullets or short paragraphs). Newest first.

---

## 2026-03-28 — plan-mode stall fix (merged plan): harness, idle, HTTP, prompts, harness-only

- **`FUnrealAiAgentHarness.cpp`:** Plan-mode stream **`Finish`** always **`CompleteAssistantOnly`** (no **`CompleteToolPath`**); enforcement event `plan_finish_ignore_streamed_tool_calls` when streamed tool deltas/`tool_calls` finish reason appeared.
- **`IUnrealAiAgentHarness` / `FUnrealAiAgentHarness`:** **`NotifyPlanExecutorStarted` / `NotifyPlanExecutorEnded` / `IsPlanPipelineActive`** so headed idle abort is not skipped with **`turn_not_in_progress`** between planner and node segments while the plan executor is still **`IsRunning()`**.
- **`FUnrealAiPlanExecutor`:** **`FUnrealAiPlanExecutorStartOptions::bHarnessPlannerOnlyNoExecute`** — after valid DAG parse, **`Finish(true)`** without **`BeginNextReadyNode`**. **`UnrealAiHarnessScenarioRunner`:** env **`UNREAL_AI_HEADED_PLAN_HARNESS_PLANNER_ONLY=1`** sets the option.
- **`UnrealAiWaitTimePolicy.h` / `FUnrealAiLlmRequest` / `FOpenAiCompatibleHttpTransport`:** **`PlannerHttpRequestTimeoutSec`** (90s default) overrides generic HTTP timeout for Plan planner requests only.
- **`UnrealAiTurnLlmRequestBuilder.cpp`:** Sets **`HttpTimeoutOverrideSec`** for **`EUnrealAiAgentMode::Plan`**.
- **Prompts:** **`09-plan-dag.md`**, **`04-tool-calling-contract.md`** — planner pass must be JSON-only; no **`tool_calls`**.
- **Build:** Sources compile; full link can fail with **LNK1104** if the editor holds **`UnrealEditor-UnrealAiEditor.dll`** (close editor or **`build-editor.ps1 -Restart`**).

---

## 2026-03-28 — fine-tuning log + plan doc hygiene

- **Plan (`plan_mode_stall_fix_3c8da223.plan.md`):** Verification assumes no harness reruns while another headed batch may run; ship notes go into this file as short factual bullets. Added final step: clean up this log (done in same pass).
- **This file:** Removed the old fixed “how to add an entry” schema and the bottom **long-running harness vs catalog** gap-audit appendix. Historical entries below were shortened; substance preserved.

---

## 2026-03-29 — plan-mode stall: idle abort, planner Finish, telemetry

- **`UnrealAiWaitTimePolicy.h`:** `HarnessSyncIdleAbortMs` 45000 → 3000 (headed sync exits soon after quiet telemetry).
- **`FUnrealAiAgentHarness.cpp`:** Plan mode — on `Finish` with `finish_reason != tool_calls`, `PendingToolCalls.Reset()` so stray `tool_calls` deltas don’t force `CompleteToolPath` / extra `DispatchLlm` without `Succeed`. `ShouldSuppressIdleAbort()` false for Plan unless `CompletedToolCallQueue` non-empty.
- **`UnrealAiHarnessScenarioRunner.cpp` — `TryHarnessIdleAbort`:** `idle_abort_skip_*` via `SetIdleAbortSkipReason`; if HTTP idle unset (`-1`), require assistant-only idle instead of blocking abort.
- **`UnrealAiHarnessProgressTelemetry`:** `NotifyHttpStreamParseComplete()` after parse; `BuildHarnessSyncTimeoutDiagnosticJson` includes `idle_abort_skip_reason`.
- **`FOpenAiCompatibleHttpTransport.cpp`:** Calls `NotifyHttpStreamParseComplete` after body parse.

**Run-11 note:** Mixed-salad plan step hit 300s sync with `plan_sub_turn_completions_before_timeout: 0` — planner JSON finished streaming but harness didn’t terminal-success; attributed to non-empty `PendingToolCalls` with `ToolsJson []` plus idle abort blocked when HTTP idle was `-1`.

---

## 2026-03-29 — HTTP 400 orphan `tool` rows

- **`UnrealAiConversationJson.cpp`:** `RemoveOrphanToolMessages` after stripping leading tool-after-system — drops `role=tool` when preceding assistant has no matching serializable `tool_call_id` (fixes OpenAI 400).
- **`UnrealAiTurnLlmRequestBuilder.cpp` — `TrimApiMessagesForContextBudget`:** If oldest post-system is assistant without serializable tool_calls followed only by tools, remove that assistant + trailing tools together.

---

## 2026-03-29 — asset referencers/deps: `path` alias

- **`UnrealAiToolDispatch_GenericAssets.cpp`:** `asset_find_referencers` / `asset_get_dependencies` accept `path` when `object_path` empty; clearer error if both missing.
- **`UnrealAiToolCatalog.json`:** Descriptions clarify `object_path` vs `content_browser_sync_asset`’s `path`.
- **`04-tool-calling-contract.md`:** Short subsection on path parameter names + minimal referencers example.

---

## 2026-03-28 — idle abort + plan sub-turn sync

- **`FUnrealAiPlanExecutor.cpp`:** Removed premature `OnPlanHarnessSubTurnComplete()` right after `RunTurn` (was schedule-time, not planner done).
- **`UnrealAiWaitTimePolicy.h`:** `HarnessSyncIdleAbortMs` (default 45s, 0 = off).
- **`ILlmTransport` / `FOpenAiCompatibleHttpTransport`:** `HasActiveRequest()` so idle abort doesn’t fire during HTTP.
- **`FUnrealAiAgentHarness`:** `ShouldSuppressIdleAbort()` during tool execution / queued tool work / incomplete stream slots.
- **`UnrealAiHarnessScenarioRunner.cpp`:** Idle predicate in sync waits; `harness_sync_idle_abort_diagnostic` JSONL.
- **`UnrealAiHarnessProgressTelemetry`:** `GetStreamIdleSeconds`, idle-abort helpers.

---

## 2026-03-28 — liberal HTTP + harness sync defaults

- **`UnrealAiRuntimeDefaults.h`:** `HttpRequestTimeoutSec` 1200s; `HarnessSyncWaitMs` ~25 min/segment; `StreamToolIncompleteMaxMs` 180000 (source-only, not `.env`).
- **`FOpenAiCompatibleHttpTransport.cpp`:** Chat timeout from `HttpRequestTimeoutSec` only.
- **`UnrealAiHarnessScenarioRunner.cpp`:** Sync uses `HarnessSyncWaitMs` only.
- **`run-long-running-headed.ps1`:** No timeout env; editor focus off by default (`-BringEditorToForeground` opt-in).

---

## 2026-03-28 — streamed tool_calls: placeholders + caps

- **`FUnrealAiAgentHarness.cpp`:** `MergeToolCallDeltas` pads placeholder slots; placeholders (empty id/name/args) ignored for incomplete timeout. First-seen maps keyed by pending index only.
- **`UnrealAiRuntimeDefaults.h`:** `StreamToolIncompleteMaxEvents` 12→96, `StreamToolIncompleteMaxMs` 2500→90000.

---

## 2026-03-28 — streamed tool timeout key + script label

- **`FUnrealAiAgentHarness.cpp`:** `Slot.StreamMergeIndex` set in merge; timeout maps use same key as first-seen (fixes `age_ms` stuck at 0).
- **`run-long-running-headed.ps1`:** Batch banner label `tool_finish_events` (was misleading `tool_calls`).

---

## 2026-03-28 — viewport framing errors

- **`UnrealAiToolDispatch_Viewport.cpp`:** `viewport_frame_actors` rejects bad `actor_paths` (`PersistentLevel` / `WorldSettings` alone, empty); suggests `scene_fuzzy_search`. `viewport_frame_selection` errors point to discovery + camera tools.
- **`UnrealAiToolCatalog.json`:** Summaries/failure_modes aligned.
- **`04-tool-calling-contract.md`:** Example path + “never PersistentLevel alone”.

---

## 2026-03-28 — Plan mode: per-segment sync; catalog `fast` removed

- **`UnrealAiHarnessScenarioRunner.cpp`:** Plan waits per segment (`PlanSubTurnEvent` + `WaitForDoneOrPlanSubTurnWhilePumpingGameThread`), fresh `HarnessSyncWaitMs` each segment.
- **`IAgentRunSink.h` / `FAgentRunFileSink`:** `OnPlanHarnessSubTurnComplete()`; executor emits after planner continuation and on node boundaries (not when pause-for-build).
- **`FUnrealAiPlanExecutor.cpp`:** Wiring above.
- **`UnrealAiToolCatalog.json` / `UnrealAiToolCatalog.cpp`:** Removed redundant `modes.fast`; Agent mode no longer falls back to `fast`.

---

## 2026-03-28 — catalog nudges (localized)

- **`UnrealAiToolCatalog.json`:** `asset_index_fuzzy_search` — prefer one bounded fuzzy call over asking user first; `material_get_usage_summary` — retry fuzzy search before “not found”.

---

## 2026-03-28 — empty `{}`, schema-first prompts, catalog truth

**Goal:** Fewer invalid tool calls (empty `{}`, missing required fields) and fewer retry loops.

**Testing:** Headed long-running harness under `tests/long-running-tests/runs/`; `harness-classification.json` is coarse; `run.jsonl` is precise for arguments.

- **`UnrealAiToolCatalog.json`:** `content_browser_sync_asset` — resolvable object path, not “folder string”; viewport frame tools — `actor_paths` required vs selection variant.
- **`UnrealAiToolDispatch_Context.cpp` — `content_browser_sync_asset`:** Empty args → `suggested_correct_call` → `asset_index_fuzzy_search`.
- **`UnrealAiToolDispatch_Viewport.cpp` — `viewport_frame_actors`:** Missing paths → `ErrorWithSuggestedCall` toward selection / search.
- **`04-tool-calling-contract.md`:** Don’t use `{}` when schema has required fields; “Required arguments (schema-first)”; examples for sync + frame actors.
- **`05-context-and-editor.md`:** Selection/framing/sync need concrete paths from context or discovery.

---

## 2026-03-28 — asset fuzzy search perf + registry guard + HTTP 400 logging

**Core fixes:** `asset_index_fuzzy_search` uses `EnumerateAssets` with `max_assets_to_scan` visit cap (default 12000) — no full `/Game` `GetAssets` array. Empty `query` without explicit `path_prefix` rejected with suggestion. `asset_registry_query` rejects totally empty filter. Blueprint/material “path required” tools use `ErrorWithSuggestedCall`. HTTP 400 logs outbound JSON parse_ok + head/tail for malformed-body diagnosis.

*Earlier prompt/catalog/resolver-only passes helped but didn’t cap CPU; enumeration + gates address stalls.*

- **`UnrealAiToolDispatch_Search.cpp`:** Enumeration + cap; narrow-search rule for empty query.
- **`UnrealAiToolDispatch_Context.cpp`:** Registry query guard.
- **Blueprint/material dispatch:** `ErrorWithSuggestedCall` when path missing.
- **`04-tool-calling-contract.md`:** Notes aligned with handlers.
- **`FOpenAiCompatibleHttpTransport.cpp`:** 400 diagnostic logging.

---

## 2026-03-28 — harness lax policy, runner logs, HTTP 429

- **`FUnrealAiAgentHarness.cpp`:** Lax policy — no hard fail on text-only after tools; telemetry events instead of synthetic nudges; mutation read-only notes telemetry-only.
- **`run-long-running-headed.ps1`:** `editor_console_saved.log` / batch log copies; `-MaxSuites`.
- **`FOpenAiCompatibleHttpTransport.cpp`:** 429 retries honor `Retry-After` / HTTP-date / JSON hints without arbitrary 120s cap when hint present.

---

<!-- New entries above this line (newest first). -->
