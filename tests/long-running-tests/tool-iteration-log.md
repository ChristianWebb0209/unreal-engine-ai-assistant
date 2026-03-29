# Tool Iteration Log

Chronicle of changes aimed at headed harness quality and API reliability. Entries are **numbered** (`Entry N`) with a short title; body is freeform bullets or paragraphs. **Newest changes appear first** (highest entry number at the top). When adding a note, prepend **`## Entry M — …`** where **M** is one greater than the current highest number (do not renumber existing entries).

---

## Entry 35 — Plan DAG stuck on first node: write node status on parent thread

- **Symptom:** After a valid planner DAG (e.g. 4 nodes `a`–`d`), `BeginNextReadyNode` kept dispatching **`node_id=a`** for dozens of LLM rounds until **`harness_scenario_wall_exceeded`** (~5 min) or huge `run.jsonl`. `harness_progress.log` showed repeated **`…_plan_a`** HTTP outbounds only; **`GetReadyNodeIds`** never saw **`success`** for `a` on the **parent** session.
- **Cause:** **`IAgentContextService::SetPlanNodeStatus`** updates **`Sessions[(ActiveProjectId, ActiveThreadId)]`**. A plan-node **`RunTurn`** uses **`ThreadId = "<parent>_plan_<node>"`** and the service’s **active** session follows that child thread. **`OnNodeFinished`** called **`SetPlanNodeStatus(..., success)`** while active was still the **child**, so completion lived on the **wrong** session. **`BeginNextReadyNode`** reads **`GetState(parent)`** for **`PlanNodeStatusById`**. **`ClearPlanStaleRunningMarkers(parent)`** (when the harness is idle) removes **`running`** from the parent map **without** a matching **`success`** there → **`a`** stays eligible forever.
- **Fix (`FUnrealAiPlanExecutor::OnNodeFinished`):** Call **`ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId)`** immediately **before** **`SetPlanNodeStatus`** so **success/failed** (and summaries) are stored on the **same thread** the DAG scheduler uses. **`CascadeSkipDependentsAfterFailure`** already did **`LoadOrCreate(parent)`** before skipped nodes; the main success path was missing it.
- **Verification:** Headed **`plan-mode-smoke`** — expect **`a` → `b` → `c` → `d`** in **`harness_progress.log` / `llm_requests.jsonl`**, **`run_finished.success`** true, ~tens of seconds instead of wall timeout. **`git`:** `fc1f00d` (same commit may include related **`OnHarnessProgressLog`** wiring in the plan executor for diagnostics).

---

## Entry 34 — Plan node empty assistant: skip harness nudge loop; `11-plan-node-execution` prompt

- **`FUnrealAiAgentHarness::FAgentTurnRunner::CompleteAssistantOnly`:** For **Agent** turns whose **`ThreadId` contains `_plan_`**, the **empty-assistant** path no longer injects **`[Harness][reason=empty_assistant]`** and **`DispatchLlm()`** in a loop (that behavior remains for normal interactive Agent chat). Plan DAG nodes run **serially**; repeated LLM rounds on empty streaming blocked the plan executor. Instead the harness emits **`plan_node_empty_assistant_finish`** (enforcement) and **completes the run** so **`OnRunFinished`** / the plan runner can advance.
- **`prompts/chunks/11-plan-node-execution.md`:** Clarifies that plan-node turns differ from interactive Agent mode; **empty assistant replies stall the pipeline**; model should always emit at least one short sentence when it would otherwise finish empty.
- **Build:** `./build-editor.ps1 -Headless` (or `-Restart` if LNK1104) after pulling this change.

---

## Entry 33 — `FAgentRunFileSink`: `bFinished` release when `run_finished` append fails (verify headed smoke)

- **`FAgentRunFileSink::OnRunFinished`:** After **`compare_exchange`** claims the terminal slot, if **`AppendRunFinishedLineWithRetry`** returns false, **`bFinished`** is **cleared** so **`~FAgentRunFileSink`** can call **`OnRunFinished`** again and retry the line (avoids a state where the disk never gets `run_finished` but `bFinished` blocks the destructor path). **`DoneEvent` / completion pointers** still run so the headed runner does not deadlock. Warning log: `run_finished append failed; released finish slot for destructor retry`.
- **Destructor comment** updated to describe append-failure vs successful single terminal.
- **Build:** `./build-editor.ps1 -Headless` (or `-Restart` if LNK1104) applied after this change.
- **Headed verification (operator):** Re-run **`tests/long-running-tests/plan-mode-smoke/`**; confirm `step_01/run.jsonl` contains `"type":"run_finished"` (see `Test-RunJsonlFinished` in `run-long-running-headed.ps1`). Inspect **`success`** / **`error_message`** as needed. Optional: confirm `llm_requests.jsonl` still shows **`## Original request (from user)`** on `*_plan_*` threads (plan-node work, **Entry 31**).

---

## Entry 32 — `FAgentRunFileSink`: forced `run_finished`, append retries

- **`FAgentRunFileSink` (`FAgentRunFileSink.cpp` / `.h`):** **`~FAgentRunFileSink`** calls **`OnRunFinished(false, …)`** when the sink is destroyed without a prior normal terminal — headed batch scripts use `"type":"run_finished"` in `run.jsonl` (`Test-RunJsonlFinished`); duplicate **`OnRunFinished`** is suppressed after a **successful** disk write (**Entry 33** handles failed append + destructor retry).
- **Append path:** **`AppendJsonObject`** returns **`bool`**, logs a **Warning** on failure (when logging enabled for that call), and **`AppendRunFinishedLineWithRetry`** writes the **`run_finished`** line with up to **four** attempts (10 ms between retries) plus an **Error** if all fail; **`DoneEvent` / completion pointers** still run after the append attempt so the sync runner does not deadlock.

---

## Entry 31 — Plan-mode node relevance: parent goal in user text, DAG merge for child threads, complexity/query alignment

- **`FUnrealAiPlanExecutor::BeginNextReadyNode`:** Plan-node `UserText` is no longer the short synthetic line only. It now leads with **`## Original request (from user)`** (from `OriginalPlannerUserText` / parent request, **4k char cap**), then **`## Current plan node`** with execute/title/hint and a **this-node-only** instruction aligned with the user goal.
- **`FUnrealAiContextService::BuildContextWindow`:** For thread ids matching `<parent>_plan_<nodeId>` (parent parsed via **last** `_plan_` marker), merge **parent** state into the working copy for the build: **`ActivePlanDagJson`**, **`PlanNodeStatusById`**, **`PlanNodeSummaryById`** — so packed context and Plan DAG candidates see the full graph on node turns (child persistence alone was empty).
- **`FUnrealAiAgentTurnRequest`:** Optional **`ContextComplexityUserText`** — when non-empty, used for retrieval prefetch, **`UserMessageForComplexity`**, usage query hash fallback, and **tiered tool-surface** hybrid query (otherwise **`UserText`**). Plan nodes leave it empty; expanded **`UserText`** carries the full task for tiering/memory.
- **Prompts:** **`09-plan-dag.md`**, **`02-operating-modes.md`** — hints must trace to the user message; discourage vague “verify setup” filler. **`11-plan-node-execution.md`** — original request block referenced; snapshot loop guidance.
- **Verification:** `./build-editor.ps1 -Headless` after C++ edits. **Headed** `plan-mode-smoke`: user runs locally; inspect `llm_requests.jsonl` for `*_plan_*` rows (**parent goal** + tool surface telemetry).

---

## Entry 30 — Run-29 failures: harness idle, prompts, catalog, suite turns

- **`UnrealAiWaitTimePolicy.h`:** **`HarnessSyncIdleAbortMs`** **3000 → 12000** — headed sync idle abort was tight for post-tool assistant completion (run-29 **step_03** missing `run_finished`).
- **Prompts:** **`10-mvp-gameplay-and-tooling.md`** — multiple-match line now includes **`blueprint_apply_ir`** in the forbidden-guess list. **`04-tool-calling-contract.md`** — multiple-asset hits explicitly name **`blueprint_apply_ir`**, **`asset_find_referencers`**, **`asset_get_dependencies`**, etc.
- **`UnrealAiToolCatalog.json`:** **`blueprint_apply_ir`** / **`asset_find_referencers`** summaries: use **`object_path`** from discovery only.
- **`realistic-user-agent-basket-rerun/suite.json`:** Turn 1 — explicit discover → **`blueprint_apply_ir`** only with paths from tool output. Turn 3 — **`asset_find_referencers`** with path from discovery + conclude without unrelated tool chains.
- **Doc:** [`run-29-basket-followup.md`](run-29-basket-followup.md) lists these under **Fixes applied (Entry 30)**.
- **Build:** Recompile plugin (`.\build-editor.ps1 -Headless` or `-Restart` if LNK1104) before re-running the headed basket.

---

## Entry 29 — Run-29 follow-up doc + basket `coverage_notes`

- **[`tests/long-running-tests/run-29-basket-followup.md`](run-29-basket-followup.md):** Expanded with baseline table (run-29 per-step classification), root-cause notes (apply_ir discovery, referencers terminal), run-29 vs **current 6-step** mapping, rerun commands (`run-long-running-headed.ps1`, `classify_harness_run_jsonl.py`), success criteria, glossary (IR), and pointers to Entries 22/24/28.
- **`realistic-user-agent-basket-rerun/suite.json`:** `coverage_notes` now point at the follow-up doc and summarize the 6 agent turns without plan-mode wording.
- **Ready to re-run:** No plugin code change required for a **progress check**—rebuild if prompts/catalog were edited since last compile; then headed batch on `realistic-user-agent-basket-rerun` and classify. Compare to [`runs/run-29-20260328-212119_364/harness-classification.json`](runs/run-29-20260328-212119_364/harness-classification.json) (target: 6/6 `run_finished`, 0 failed suite, minimal `tool_finish_false`).

---

## Entry 28 — `new-test-basket-04` / `06` + `realistic-user-agent-basket-rerun` trims

- **`new-test-basket-04`:** **9** agent turns; `suite_id` suffix `pcg_natural_lead_in`.
- **`new-test-basket-06`:** **7** turns (6 agent + 1 ask).
- **`realistic-user-agent-basket-rerun`:** **6** agent turns; `coverage_notes` aligned to current steps.
- **Six-basket line count:** **56** turn lines across **`new-test-basket-01`–`06`**. Run-29 follow-up: [`run-29-basket-followup.md`](run-29-basket-followup.md).

---

## Entry 27 — Six `new-test-basket-NN` slices (≤10 turns each)

- **Replaces** the former three large baskets (`new-test-basket-1` … `3`, removed): same global order split into **`new-test-basket-01`** … **`new-test-basket-06`** for faster iteration (see **Entry 28** for current line counts). `suite_id` values end in `_of_06_*`; `coverage_notes` give slice scope.

---

## Entry 26 — `passed-tests` vs `regression-watchlist`, three `new-test-basket-*` suites

- **`tests/long-running-tests/passed-tests/suite.json`:** Trimmed to **11 high-confidence** turns (manifest, level sequence honest skip, handoff, ask grounding, multi-step viewport, `DefaultEngine.ini` peek, source symbol search, ask reflection, shader compile wait, run-13 coffee + forward trace with clean spelling). **Removed** chain-dependent, write-heavy, typo-resolution, and thin-content risks to the watchlist suite.
- **`tests/long-running-tests/regression-watchlist/suite.json`:** New suite (**not** named “shaky tests”) for **13** turns that are still useful regressions but **project-sensitive** or historically uneven (actor move, MI open, viewport typo framing, duplicate, fuzzy typo, PlayerStart search, blueprint peek, explicit physics trace, run-13 character-BP chain, deps/overview, visibility, AnimBP skim).
- **Superseded by Entry 27:** Former **`new-test-basket-1` … `new-test-basket-3`** held migrated content from **`gap-tools-coverage`**, **`gap-tools-extended`**, **`fine-tune-05-gap-tools`**; now use **`new-test-basket-01` … `06`** (see Entry 27).
- **Docs:** `coverage_notes` in each JSON describe scope; Entry 24 still references older `passed-tests` size—superseded by this split.

---

## Entry 25 — Subagents architecture doc + `bUseSubagents` setting (default on)

- **`docs/planning/subagents-architecture.md`:** Current serial plan-DAG architecture; safety rules and minimum task size for future parallel nodes; phased rollout; proposed new files (`UnrealAiPlanParallelPolicy`, `FUnrealAiPlanWaveScheduler`); linked from [`docs/README.md`](../docs/README.md).
- **`UUnrealAiEditorSettings`:** **`bUseSubagents`** (`Config = Editor`, default **true**) — display **Use subagents (parallel plan nodes)**; parallel scheduling not implemented yet; when false, executor should **keep serial** once parallel exists.
- **Build:** Recompile plugin after settings change.

---

## Entry 24 — Run-29 basket postmortem, `passed-tests` suite, basket steps 5–6

- **Run-29** ([`runs/run-29-20260328-212119_364`](runs/run-29-20260328-212119_364)): **Not a full success** — `failed_suite_count` 1, `batch_all_expected_run_jsonls_finished` false, **3/5** `run.jsonl` with `run_finished`. **step_03:** `asset_find_referencers` succeeded; stream ended **without** `run_finished` (truncated log). **step_05:** Planner emitted JSON + 3 nodes; executor then ran **tools** on plan nodes (`editor_state_snapshot_read`, `scene_fuzzy_search`)—`run.jsonl` incomplete vs “prose checklist only” ask; likely timeout/editor exit before terminal. **step_01:** `blueprint_apply_ir` `tool_finish_false` then recovery (same class as earlier scratch-float runs).
- **`tests/long-running-tests/passed-tests/suite.json`:** Expanded to **~23 turns** from archived `workflow-input.json` across **run-22** (Entry 20 basket drops), **run-20** (Entry 17 duplicate + typo drops), **full mixed-salad** vs lean (ask + long viewport + scene + blueprint peek + ini + source + reflection), **run-13** basket turns 1–5 (passed per run-20 focus), plus **fine-tune-05** lines (shader compile wait, visibility, anim BP read). See file `coverage_notes` for paths.
- **`realistic-user-agent-basket-rerun/suite.json`:** Inserted **step_5** `.uproject` read, **step_6** viewport forward trace; plan turn now **step_7**; `coverage_notes` summarize run-29 and next steps.

---

## Entry 23 — Plan DAG hardening: repair loop, idle abort, plan-node prompts (audit completion)

- **`FUnrealAiPlanExecutor`:** One-shot **planner repair** after invalid JSON or failed `ValidateDag` (augmented user text via `MakePlannerDagRepairUserText`); `OriginalPlannerUserText` captured in `Start()`. Clearer `Finish` strings for `ResumeExecutionFromDag` parse/validate. Plan child `LlmRoundBudgetFloor` aligned with `PlanNodeMaxLlmRounds`.
- **`FUnrealAiAgentHarness::DispatchLlm`:** Caps `EffectiveMaxLlmRounds` at `PlanNodeMaxLlmRounds` for Agent turns whose `threadId` contains `_plan_`.
- **`UnrealAiWaitTimePolicy.h`:** `HarnessPlanPipelineSyncIdleAbortMs` (45s default), `PlanNodeMaxLlmRounds` (12).
- **`UnrealAiHarnessScenarioRunner`:** `GetEffectiveHarnessSyncIdleAbortMs` when `IsPlanPipelineActive`; idle-abort diagnostics use the **effective** ms (not always 3s).
- **Prompts:** `02-operating-modes.md` (Plan checklist scope), `09-plan-dag.md` (v1 cost), new `11-plan-node-execution.md`; `UnrealAiPromptBuilder` / `UnrealAiTurnLlmRequestBuilder` inject chunk 11 for `*_plan_*` Agent threads.
- **Docs:** Plan DAG behavior also described in prompt chunks and `docs/planning/subagents-architecture.md` (repair, plan-node rounds, plan-pipeline idle abort were covered in code + chunks).
- **Build:** `.\build-editor.ps1 -Restart -Headless` after changes; use **`-Restart`** if `LNK1104` on the plugin DLL.
- **Headed verification:** User runs `plan-mode-smoke` locally (not automated here).

---

## Entry 22 — Basket suite hardening: multi-match prompts + expanded realistic-user-agent turns

- **Problem (run-25):** `asset_index_fuzzy_search` returned multiple Blueprints; model called **`blueprint_export_ir`** with a path not in the result set (`SimpleBlueprint`), then recovered—**`tool_finish_false`** still counted 1 on [`tests/long-running-tests/runs/run-25-20260328-210131_814`](tests/long-running-tests/runs/run-25-20260328-210131_814) / [`harness-classification.json`](tests/long-running-tests/runs/run-25-20260328-210131_814/harness-classification.json) (`tool_finish_false`: 1, `run_finished_true`: 1 for the single `step_01` `run.jsonl`).
- **`10-mvp-gameplay-and-tooling.md`:** New bullet **Multiple fuzzy/registry matches**—only use **`object_path`** values from the discovery result; pick from the list or narrow the query; no invented `/Game/...`; no **`blueprint_export_ir` / `blueprint_get_graph_summary` / `blueprint_compile`** on guessed names.
- **`04-tool-calling-contract.md`:** New bullet **Multiple asset hits** under discovery.
- **`UnrealAiToolCatalog.json`:** **`asset_index_fuzzy_search`** and **`blueprint_export_ir`** summaries extended with the same constraint (downstream paths must come from results).
- **`tests/long-running-tests/realistic-user-agent-basket-rerun/suite.json`:** Expanded from one turn to **five**: (1) scratch-float “that blueprint” regression; (2) gameplay BP disambiguation + scratch bool; (3) referencer count after discovery; (4) Material Instance roughness tweak with honest skip if no MI; (5) **plan** turn—small orientation checklist DAG aligned with **`02-operating-modes.md`**. Did **not** restore old run-22 steps 2–7 (Entry 20 easy passes). `coverage_notes` updated for what to measure (`tool_finish_false`, discovery order, plan node count).
- **Validation:** `run-long-running-headed.ps1 -ScenarioFolder tests\long-running-tests\realistic-user-agent-basket-rerun -DryRun` succeeds (**5** turns). Re-run **headed** (no `-DryRun`) locally to produce `runs/run-*` with per-step `run.jsonl`, then `python tests/classify_harness_run_jsonl.py --batch-root <that folder>` and compare per-step **`tool_finish_false`** to the run-25 baseline above.
- **Build:** JSON + markdown only for this entry; no C++ changes in this change set.

---

## Entry 21 — Log format: numbered entries (replace date headings)

- Replaced **`## YYYY-MM-DD — …`** section titles with **`## Entry N — …`** (`Entry 1` = oldest, `Entry 21` = this change; file order remains newest first).
- **`docs/README.md`**, **`context.md`**, **`docs/tooling/tool-catalog-audit-guide.md`:** Point to [`tests/long-running-tests/tool-iteration-log.md`](tool-iteration-log.md) and describe numbering + prepend rule.

---

## Entry 20 — Run-22 follow-up: suite minimal trim, “that blueprint” prompt, editor exit policy

- **`tests/long-running-tests/realistic-user-agent-basket-rerun/suite.json`:** Single turn only—scratch-float Blueprint request (run-22 step_01 with tool_fail events). Dropped steps 2–7 (run-22 had zero `tool_finish_false` there). `coverage_notes` updated for run-22–minimal.
- **`10-mvp-gameplay-and-tooling.md`:** Bullet **“That blueprint” with no path:** use **`editor_get_selection`** / **`asset_index_fuzzy_search`** and returned **`object_path`** before **`blueprint_get_graph_summary`** / **`blueprint_apply_ir`** / **`blueprint_compile`**—no assumed tutorial names.
- **`run-long-running-headed.ps1`:** Documented that **`batch_editor_exit_code`** from Unreal often ≠ harness failure when all `run_finished` succeeded. Optional **`UNREAL_AI_HEADED_BATCH_IGNORE_EDITOR_NONZERO_EXIT=1`** sets batch pass if all expected `run.jsonl` finished and **`failed_suite_count`** is 0, even when editor process exits nonzero.
- **Build:** Prompt/suite/PS1 only—no plugin compile required for this change set.

---

## Entry 19 — Prompt policy: slim duplicate cross-refs (after audit)

- **Keep:** **`01-identity.md`** **Examples contract** (one place); **`04-tool-calling-contract.md`** **Discovery before targeted calls** + placeholder-style minimal JSON; **`06`**/**`09`** short chunk-specific lines (plan hint ≠ path); **`10`** known-target wording; **`README`** canonical-behavior bullet.
- **Remove:** Repeated “see 01+04” boilerplate from **`02`**, **`05`**, **`07`**, **`08`**, and duplicate opening bullet from **`10`**.
- **`00-template-tokens.md`:** Author note only—no copy-pastable `/Game/...` tutorial names in chunk examples.
- **Rationale:** One strong rule + scrub literals beats repeating prose in many chunks ([`prompts/README.md`](Plugins/UnrealAiEditor/prompts/README.md) **Canonical behavior**).

---

## Entry 18 — Prompt chunk audit: discovery + identifiers (cross-chunk consistency)

- **Invariant:** **`01-identity.md`** + **`04-tool-calling-contract.md`** (**Discovery before targeted calls**) are canonical; other chunks cross-reference instead of conflicting rules.
- **`00-template-tokens.md`:** Note that behavioral rules live in numbered chunks, not the token table.
- **`01-identity.md`:** Linked “no invented identifiers” to **04** **Discovery before targeted calls** by name.
- **`02-operating-modes.md` (Ask):** Read-only tools bullet points to **01** + **04**.
- **`03-complexity-and-todo-plan.md`:** “Unresolved path” trigger forbids inventing paths in plan/todo prose; **01**/**04** pointers.
- **`04-tool-calling-contract.md`:** Path-resolution / known-target text; minimal JSON examples use `<DiscoveredBp>` / `<DiscoveredMI>` / search query `enemy` instead of `BP_Player`/`BP_Enemy`/`MI_Player` literals.
- **`05-context-and-editor.md`:** New **Identifiers** line tying attachments to **01**/**04**.
- **`06-execution-subturn.md`:** Plan `hint` text is not a substitute for real tool paths.
- **`07-safety-banned.md`:** Ground truth extended to asset existence claims.
- **`08-output-style.md`:** User-visible `/Game` paths must reflect tools/context.
- **`09-plan-dag.md`:** Node hints are not validated paths for execution turns.
- **`10-mvp-gameplay-and-tooling.md`:** Opening principle + tightened known-target shortcuts.
- **`prompts/README.md`:** Design-rules **Invariant** bullet for discovery/identifiers.

---

## Entry 17 — run-20 follow-up: suite trim, anti-example-leak, TTFT idle skip, SSE tool event cap

- **`tests/long-running-tests/realistic-user-agent-basket-rerun/suite.json`:** Dropped run-20 easy-pass turns (duplicate under `/Game`, typo asset resolution); `coverage_notes` now describe the run-20–focused subset (7 turns).
- **`04-tool-calling-contract.md` / `UnrealAiToolCatalog.json`:** Minimal JSON examples framed as **shape-only**; `project_file_read_text` line uses `<actual_basename>.uproject` + rule text (no literal `MyProject.uproject` / `MyGame.uproject` in catalog summary).
- **`UnrealAiTurnLlmRequestBuilder.cpp`:** System prompt gains factual **Project workspace** line: manifest **basename** from `FPaths::GetCleanFilename(GetProjectFilePath())` so the model can ground `relative_path` without copying doc placeholders.
- **`UnrealAiHarnessScenarioRunner.cpp` — `TryHarnessIdleAbort`:** Headed scenario skips idle abort while **`HasActiveLlmTransportRequest()`** and **no assistant delta yet** (`awaiting_first_assistant_delta`); avoids false abort when time-to-first-token exceeds **`HarnessSyncIdleAbortMs`** (3s).
- **`UnrealAiWaitTimePolicy.h`:** **`StreamToolIncompleteMaxEvents`** 64 → **128** — run-20 step_01 hit cap at **`age_events=64`** (`age_ms=4`, fragmented SSE) before tool JSON closed.

---

## Entry 16 — run-13 class: `project_file_read_text` suggestion + harness `suggested_correct_call` nudge

- **`UnrealAiToolDispatch_ProjectFiles.cpp`:** Missing `relative_path` for **`project_file_read_text`** now builds **`suggested_correct_call`** from **`FPaths::GetProjectFilePath()`** (project-relative via **`FPaths::MakePathRelativeTo`**), not a hardcoded **`Config/DefaultEngine.ini`** only; clearer error text mentions `.uproject` manifest vs config.
- **`UnrealAiToolCatalog.json` / `04-tool-calling-contract.md`:** Catalog summary + one minimal **`project_file_read_text`** example with **`MyProject.uproject`**.
- **`FUnrealAiAgentHarness.cpp`:** When **`RepeatedToolFailureCount >= 3`**, the existing **`[Harness][reason=repeated_validation_failure]`** line may append the last resolver **`suggested_correct_call`** JSON (capped); enforcement event **`suggested_call_validation_nudge`**. **`LastSuggestedCorrectCallSerialized`** resets each LLM round and on tool success.
- **`UnrealAiToolDispatchAutomationTests.cpp`:** Asserts suggested **`relative_path`** matches **`FPaths::GetCleanFilename(GetProjectFilePath())`** when the project file is known.
- **Truncated `run.jsonl` (e.g. run-13 step_06 / step_17):** No code change; if needed, correlate with **`editor_console_saved.log`** / batch exit for that run.

---

## Entry 15 — plan-mode stall fix (merged plan): harness, idle, HTTP, prompts, harness-only

- **`FUnrealAiAgentHarness.cpp`:** Plan-mode stream **`Finish`** always **`CompleteAssistantOnly`** (no **`CompleteToolPath`**); enforcement event `plan_finish_ignore_streamed_tool_calls` when streamed tool deltas/`tool_calls` finish reason appeared.
- **`IUnrealAiAgentHarness` / `FUnrealAiAgentHarness`:** **`NotifyPlanExecutorStarted` / `NotifyPlanExecutorEnded` / `IsPlanPipelineActive`** so headed idle abort is not skipped with **`turn_not_in_progress`** between planner and node segments while the plan executor is still **`IsRunning()`**.
- **`FUnrealAiPlanExecutor`:** **`FUnrealAiPlanExecutorStartOptions::bHarnessPlannerOnlyNoExecute`** — after valid DAG parse, **`Finish(true)`** without **`BeginNextReadyNode`**. **`UnrealAiHarnessScenarioRunner`:** env **`UNREAL_AI_HEADED_PLAN_HARNESS_PLANNER_ONLY=1`** sets the option.
- **`UnrealAiWaitTimePolicy.h` / `FUnrealAiLlmRequest` / `FOpenAiCompatibleHttpTransport`:** **`PlannerHttpRequestTimeoutSec`** (90s default) overrides generic HTTP timeout for Plan planner requests only.
- **`UnrealAiTurnLlmRequestBuilder.cpp`:** Sets **`HttpTimeoutOverrideSec`** for **`EUnrealAiAgentMode::Plan`**.
- **Prompts:** **`09-plan-dag.md`**, **`04-tool-calling-contract.md`** — planner pass must be JSON-only; no **`tool_calls`**.
- **Build:** Sources compile; full link can fail with **LNK1104** if the editor holds **`UnrealEditor-UnrealAiEditor.dll`** (close editor or **`build-editor.ps1 -Restart`**).

---

## Entry 14 — fine-tuning log + plan doc hygiene

- **Plan (`plan_mode_stall_fix_3c8da223.plan.md`):** Verification assumes no harness reruns while another headed batch may run; ship notes go into this file as short factual bullets. Added final step: clean up this log (done in same pass).
- **This file:** Removed the old fixed “how to add an entry” schema and the bottom **long-running harness vs catalog** gap-audit appendix. Historical entries below were shortened; substance preserved.

---

## Entry 13 — plan-mode stall: idle abort, planner Finish, telemetry

- **`UnrealAiWaitTimePolicy.h`:** `HarnessSyncIdleAbortMs` 45000 → 3000 (headed sync exits soon after quiet telemetry).
- **`FUnrealAiAgentHarness.cpp`:** Plan mode — on `Finish` with `finish_reason != tool_calls`, `PendingToolCalls.Reset()` so stray `tool_calls` deltas don’t force `CompleteToolPath` / extra `DispatchLlm` without `Succeed`. `ShouldSuppressIdleAbort()` false for Plan unless `CompletedToolCallQueue` non-empty.
- **`UnrealAiHarnessScenarioRunner.cpp` — `TryHarnessIdleAbort`:** `idle_abort_skip_*` via `SetIdleAbortSkipReason`; if HTTP idle unset (`-1`), require assistant-only idle instead of blocking abort.
- **`UnrealAiHarnessProgressTelemetry`:** `NotifyHttpStreamParseComplete()` after parse; `BuildHarnessSyncTimeoutDiagnosticJson` includes `idle_abort_skip_reason`.
- **`FOpenAiCompatibleHttpTransport.cpp`:** Calls `NotifyHttpStreamParseComplete` after body parse.

**Run-11 note:** Mixed-salad plan step hit 300s sync with `plan_sub_turn_completions_before_timeout: 0` — planner JSON finished streaming but harness didn’t terminal-success; attributed to non-empty `PendingToolCalls` with `ToolsJson []` plus idle abort blocked when HTTP idle was `-1`.

---

## Entry 12 — HTTP 400 orphan `tool` rows

- **`UnrealAiConversationJson.cpp`:** `RemoveOrphanToolMessages` after stripping leading tool-after-system — drops `role=tool` when preceding assistant has no matching serializable `tool_call_id` (fixes OpenAI 400).
- **`UnrealAiTurnLlmRequestBuilder.cpp` — `TrimApiMessagesForContextBudget`:** If oldest post-system is assistant without serializable tool_calls followed only by tools, remove that assistant + trailing tools together.

---

## Entry 11 — asset referencers/deps: `path` alias

- **`UnrealAiToolDispatch_GenericAssets.cpp`:** `asset_find_referencers` / `asset_get_dependencies` accept `path` when `object_path` empty; clearer error if both missing.
- **`UnrealAiToolCatalog.json`:** Descriptions clarify `object_path` vs `content_browser_sync_asset`’s `path`.
- **`04-tool-calling-contract.md`:** Short subsection on path parameter names + minimal referencers example.

---

## Entry 10 — idle abort + plan sub-turn sync

- **`FUnrealAiPlanExecutor.cpp`:** Removed premature `OnPlanHarnessSubTurnComplete()` right after `RunTurn` (was schedule-time, not planner done).
- **`UnrealAiWaitTimePolicy.h`:** `HarnessSyncIdleAbortMs` (default 45s, 0 = off).
- **`ILlmTransport` / `FOpenAiCompatibleHttpTransport`:** `HasActiveRequest()` so idle abort doesn’t fire during HTTP.
- **`FUnrealAiAgentHarness`:** `ShouldSuppressIdleAbort()` during tool execution / queued tool work / incomplete stream slots.
- **`UnrealAiHarnessScenarioRunner.cpp`:** Idle predicate in sync waits; `harness_sync_idle_abort_diagnostic` JSONL.
- **`UnrealAiHarnessProgressTelemetry`:** `GetStreamIdleSeconds`, idle-abort helpers.

---

## Entry 9 — liberal HTTP + harness sync defaults

- **`UnrealAiRuntimeDefaults.h`:** `HttpRequestTimeoutSec` 1200s; `HarnessSyncWaitMs` ~25 min/segment; `StreamToolIncompleteMaxMs` 180000 (source-only, not `.env`).
- **`FOpenAiCompatibleHttpTransport.cpp`:** Chat timeout from `HttpRequestTimeoutSec` only.
- **`UnrealAiHarnessScenarioRunner.cpp`:** Sync uses `HarnessSyncWaitMs` only.
- **`run-long-running-headed.ps1`:** No timeout env; editor focus off by default (`-BringEditorToForeground` opt-in).

---

## Entry 8 — streamed tool_calls: placeholders + caps

- **`FUnrealAiAgentHarness.cpp`:** `MergeToolCallDeltas` pads placeholder slots; placeholders (empty id/name/args) ignored for incomplete timeout. First-seen maps keyed by pending index only.
- **`UnrealAiRuntimeDefaults.h`:** `StreamToolIncompleteMaxEvents` 12→96, `StreamToolIncompleteMaxMs` 2500→90000.

---

## Entry 7 — streamed tool timeout key + script label

- **`FUnrealAiAgentHarness.cpp`:** `Slot.StreamMergeIndex` set in merge; timeout maps use same key as first-seen (fixes `age_ms` stuck at 0).
- **`run-long-running-headed.ps1`:** Batch banner label `tool_finish_events` (was misleading `tool_calls`).

---

## Entry 6 — viewport framing errors

- **`UnrealAiToolDispatch_Viewport.cpp`:** `viewport_frame_actors` rejects bad `actor_paths` (`PersistentLevel` / `WorldSettings` alone, empty); suggests `scene_fuzzy_search`. `viewport_frame_selection` errors point to discovery + camera tools.
- **`UnrealAiToolCatalog.json`:** Summaries/failure_modes aligned.
- **`04-tool-calling-contract.md`:** Example path + “never PersistentLevel alone”.

---

## Entry 5 — Plan mode: per-segment sync; catalog `fast` removed

- **`UnrealAiHarnessScenarioRunner.cpp`:** Plan waits per segment (`PlanSubTurnEvent` + `WaitForDoneOrPlanSubTurnWhilePumpingGameThread`), fresh `HarnessSyncWaitMs` each segment.
- **`IAgentRunSink.h` / `FAgentRunFileSink`:** `OnPlanHarnessSubTurnComplete()`; executor emits after planner continuation and on node boundaries (not when pause-for-build).
- **`FUnrealAiPlanExecutor.cpp`:** Wiring above.
- **`UnrealAiToolCatalog.json` / `UnrealAiToolCatalog.cpp`:** Removed redundant `modes.fast`; Agent mode no longer falls back to `fast`.

---

## Entry 4 — catalog nudges (localized)

- **`UnrealAiToolCatalog.json`:** `asset_index_fuzzy_search` — prefer one bounded fuzzy call over asking user first; `material_get_usage_summary` — retry fuzzy search before “not found”.

---

## Entry 3 — empty `{}`, schema-first prompts, catalog truth

**Goal:** Fewer invalid tool calls (empty `{}`, missing required fields) and fewer retry loops.

**Testing:** Headed long-running harness under `tests/long-running-tests/runs/`; `harness-classification.json` is coarse; `run.jsonl` is precise for arguments.

- **`UnrealAiToolCatalog.json`:** `content_browser_sync_asset` — resolvable object path, not “folder string”; viewport frame tools — `actor_paths` required vs selection variant.
- **`UnrealAiToolDispatch_Context.cpp` — `content_browser_sync_asset`:** Empty args → `suggested_correct_call` → `asset_index_fuzzy_search`.
- **`UnrealAiToolDispatch_Viewport.cpp` — `viewport_frame_actors`:** Missing paths → `ErrorWithSuggestedCall` toward selection / search.
- **`04-tool-calling-contract.md`:** Don’t use `{}` when schema has required fields; “Required arguments (schema-first)”; examples for sync + frame actors.
- **`05-context-and-editor.md`:** Selection/framing/sync need concrete paths from context or discovery.

---

## Entry 2 — asset fuzzy search perf + registry guard + HTTP 400 logging

**Core fixes:** `asset_index_fuzzy_search` uses `EnumerateAssets` with `max_assets_to_scan` visit cap (default 12000) — no full `/Game` `GetAssets` array. Empty `query` without explicit `path_prefix` rejected with suggestion. `asset_registry_query` rejects totally empty filter. Blueprint/material “path required” tools use `ErrorWithSuggestedCall`. HTTP 400 logs outbound JSON parse_ok + head/tail for malformed-body diagnosis.

*Earlier prompt/catalog/resolver-only passes helped but didn’t cap CPU; enumeration + gates address stalls.*

- **`UnrealAiToolDispatch_Search.cpp`:** Enumeration + cap; narrow-search rule for empty query.
- **`UnrealAiToolDispatch_Context.cpp`:** Registry query guard.
- **Blueprint/material dispatch:** `ErrorWithSuggestedCall` when path missing.
- **`04-tool-calling-contract.md`:** Notes aligned with handlers.
- **`FOpenAiCompatibleHttpTransport.cpp`:** 400 diagnostic logging.

---

## Entry 1 — harness lax policy, runner logs, HTTP 429

- **`FUnrealAiAgentHarness.cpp`:** Lax policy — no hard fail on text-only after tools; telemetry events instead of synthetic nudges; mutation read-only notes telemetry-only.
- **`run-long-running-headed.ps1`:** `editor_console_saved.log` / batch log copies; `-MaxSuites`.
- **`FOpenAiCompatibleHttpTransport.cpp`:** 429 retries honor `Retry-After` / HTTP-date / JSON hints without arbitrary 120s cap when hint present.

---

<!-- Next change: prepend `## Entry 25 — …` above Entry 24 (do not renumber existing entries). -->
