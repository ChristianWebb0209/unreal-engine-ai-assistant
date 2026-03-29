# Planning in Unreal AI Editor

For **Ask vs Agent vs Plan** (mode behavior and file map), see **[`agent-modes.md`](agent-modes.md)**.

The plugin exposes **two** structured planning mechanisms. They use different JSON shapes and different code paths; do not mix them in prompts or persistence.

## 1. Plan mode (chat): `unreal_ai.plan_dag`

- **Where:** **Plan** mode in Agent Chat. The model outputs a single JSON object (no tools in the planner pass).
- **Schema:** `unreal_ai.plan_dag` with a `nodes[]` array (`id`, `title`, `hint`, `dependsOn` / `depends_on`).
- **Execution:** `FUnrealAiPlanExecutor` in `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Planning/` runs the planner turn, validates the DAG (`UnrealAiPlanDag`), then executes **ready** nodes **serially** as separate Agent-mode harness turns (`<threadId>_plan_<nodeId>`).
- **Invalid DAG:** `ParseDagJson` + `ValidateDag` run before any node executes. On failure, the executor may run **one** automatic planner **repair** pass with the same user thread and augmented user text containing the validation error (JSON-only retry). Persistent UI “Build” paths (`SChatComposer`) also reject invalid DAG JSON before `ResumeExecutionFromDag`.
- **Plan-node Agent turns** use prompt chunk `11-plan-node-execution.md` (injected when the thread id contains `_plan_`) and an **LLM round ceiling** (`UnrealAiWaitTime::PlanNodeMaxLlmRounds` in `UnrealAiWaitTimePolicy.h`, enforced in `FAgentTurnRunner::DispatchLlm`).
- **Headed harness idle abort:** default post-stream idle stall threshold is `HarnessSyncIdleAbortMs` (3s). While a plan pipeline is active (`IsPlanPipelineActive`), headed scenario sync uses `HarnessPlanPipelineSyncIdleAbortMs` (45s by default) so planner/node segments can include tool bursts and post-tool LLM work without false-triggering abort; diagnostics log the **effective** threshold.
- **Persistence:** Active DAG text and per-node status live in context service fields such as `ActivePlanDagJson` / `PlanNodeStatusById` (see [`context-management.md`](context/context-management.md)).

## 2. Agent mode: `agent_emit_todo_plan` → `unreal_ai.todo_plan`

- **Where:** **Agent** mode. The model calls the **`agent_emit_todo_plan`** tool (catalog category **`exec`**).
- **Schema:** `unreal_ai.todo_plan` with `title` and `steps[]` (see tool catalog and prompts).
- **Execution:** The harness records the plan in context; execution continues as normal agent turns. This is **not** the same executor as Plan-mode DAG execution.
- **Persistence:** `ActiveTodoPlanJson` and related UI state (todo panel).

## 3. Shared summarization (no merged storage)

- **`UnrealAiStructuredPlanSummary`** (`Private/Planning/UnrealAiStructuredPlanSummary.cpp`) formats **one-line summaries** for prompts from either persisted shape.
- **`UnrealAiActiveTodoSummary.h`** re-exports those APIs for existing includes.

There is **no** subagent spawn, worker merge, or separate orchestrator process in this product build.

---

## 4. Why orchestration / subagent tools were removed (for now)

**Goals:** Reduce moving parts, shrink the tool catalog and dispatch surface, and make Plan-mode and harness behavior easier to debug. The team had spent a long time on plan/harness edge cases; keeping **half-implemented** orchestration concepts (`subagent_spawn`, `worker_merge_results`, merge helpers) in the tree added **documentation and mental overhead** without delivering a complete parallel-delegation product.

**What stayed:** The **DAG model** (`nodes`, `depends_on`, ready-set computation) is unchanged. That layout was always the right structure for **future** fan-out (multiple ready nodes) and delegation; v1 execution remains **serial**—one ready node per step.

**What went:** Catalog entries and C++ paths for spawning subagents and merging worker blobs, plus the small deterministic merge helper/tests tied only to that path. **Plan DAG execution** and **`agent_emit_todo_plan`** were **not** removed.

---

## 5. Re-adding orchestration and subagents later

This is **intentionally** feasible without redesigning the plan format.

**Foundation (already in place):**

- `UnrealAiPlanDag::GetReadyNodeIds` can return **more than one** id when dependencies allow—serial execution today simply takes the first (or you keep a single ready queue).
- Each node already runs as its **own harness turn** with an isolated `threadId` (`<parent>_plan_<nodeId>`), which is the same isolation shape you would use for delegated workers.

**What a future “Level B” style layer would add:**

1. **Scheduling:** When multiple nodes are ready, **schedule N turns** (concurrency cap, editor/game-thread safety for mutating tools).
2. **Spawning:** Optional **catalog tools** again (e.g. `subagent_spawn`) or **internal** API that starts a child run with a **tool allowlist** and budget.
3. **Merge:** Deterministic merge of structured results (status, summary, errors, artifacts) and/or a **single** LLM merge step—only if you need a parent summary; many flows can persist per-node status in context without a separate merge tool.
4. **Persistence / UX:** Parent run id, worker ids, progress in UI, cancellation across children.

**Documentation to update when you implement:** [`tool-registry.md`](tooling/tool-registry.md), [`agent-and-tool-requirements.md`](tooling/agent-and-tool-requirements.md), [`by-category.md`](../Plugins/UnrealAiEditor/prompts/tools/by-category.md), and this file.

---

## 6. Smoke and batch testing

For **plan-mode smoke** (e.g. [`tests/long-running-tests/plan-mode-smoke/`](../tests/long-running-tests/plan-mode-smoke)) the suite uses a **natural-language** user message (plain ask for a short ordered checklist). The planner should still emit **`unreal_ai.plan_dag`** JSON per global Plan-mode prompts; the executor then runs **ready nodes serially** (typically a **few** nodes—exact count is not fixed). Re-run after refactors that touch `Planning/` or the harness. Interpretation of long batches (stalls, `tool_finish_false`, missing `run_finished`) is separate from orchestration—see [`analysis-guide.md`](../tests/long-running-tests/analysis-guide.md) and the latest `last-suite-summary.json` under `tests/long-running-tests/runs/`.
