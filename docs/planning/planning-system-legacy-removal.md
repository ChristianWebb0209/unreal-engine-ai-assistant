# Planning system refactor — remove legacy todo, unify on DAG

This document is the **product and engineering plan** for replacing the `unreal_ai.todo_plan` / `agent_emit_todo_plan` stack with a **single planning model** based on `unreal_ai.plan_dag`, while preserving the behaviors you want (user approval in Plan mode, internal scratch plans in Agent mode, serial vs parallel waves). It is **orthogonal** to the **context-management** refactor (candidate gate, ingestion split, working set): [context-ingestion-refactor.md](context-ingestion-refactor.md).

**Related docs:** [subagents-architecture.md](subagents-architecture.md) (current executor behavior, wave width, thread ids).

---

## 1. Why change

Today two representations coexist:

| System | Persistence | Execution | UI / model |
|--------|-------------|-----------|------------|
| **Legacy todo** | `ActiveTodoPlanJson`, `TodoStepsDone` in `context.json` | Harness bumps one `TodoStepsDone` flag per successful tool round (coarse, not DAG-aware) | `OnTodoPlanEmitted` → transcript `TodoPlan`; `TodoState` context candidate; `{{ACTIVE_TODO_SUMMARY}}`; optional execution subturn |
| **Plan DAG** | `ActivePlanDagJson`, `PlanNodeStatusById`, `PlanNodeSummaryById` | `FUnrealAiPlanExecutor`: ready-set waves, dependency-aware | `PlanState` candidate; plan UI / run progress |

Problems:

- Duplicate concepts (steps vs nodes), duplicate context surface (`TodoState` vs `PlanState`).
- Legacy progress logic does not respect real dependencies.
- Agent-mode “scratch” plans cannot be distinguished from user-facing todos without extra flags.

**Goal:** One canonical **DAG** for structured work; legacy fields and tool **removed** after migration.

---

## 2. Target behavior

### 2.1 Plan mode — emit, then user approval

- Planner produces (or user pastes) **`unreal_ai.plan_dag`** JSON.
- **Block node execution** until the user explicitly **approves** the plan (UI button / state machine in harness or chat).
- After approval, `FUnrealAiPlanExecutor` runs nodes using existing DAG semantics.

### 2.2 Agent mode — internal plans, not shown

- Agent may maintain a **scratch DAG** (or linear checklist encoded as a degenerate DAG) for multi-step reasoning.
- **No** transcript `TodoPlan` block and **no** `OnTodoPlanEmitted` for these internal plans unless explicitly “promoted” to user-visible.
- Context may still include a **low-priority, budgeted** summary candidate if useful, or omit entirely via ranking.

### 2.3 DAG shape; serial vs concurrent

- Structure remains a **DAG** (`dependsOn` / `depends_on`).
- **`agent.useSubagents` disabled:** wave width **1** — at each step, at most **one** ready node runs (valid topological order, deterministic tie-break). This is “flatten in dependency order” without losing dependency semantics.
- **`agent.useSubagents` enabled:** wave width **> 1** (today hardcoded **2** in `FUnrealAiPlanExecutor::Start`); optionally make this **configurable** later.

See [subagents-architecture.md](subagents-architecture.md) for current implementation details.

---

## 3. Migration strategy

1. **Read path:** On load, if `activeTodoPlan` exists in `context.json` and `activePlanDag` is empty, optionally **convert** legacy todo `steps[]` into a minimal `nodes[]` DAG (linear edges or empty `dependsOn` for independent steps) **once**, then persist as `activePlanDag` and clear legacy fields (behind schema version bump).
2. **Write path:** Stop writing `activeTodoPlan` / `TodoStepsDone` as soon as new code ships; tools only emit DAG-shaped plans.
3. **Catalog / prompts:** Remove or redirect `agent_emit_todo_plan` to a DAG tool (or single `agent_emit_plan_dag` with `visibility: user|internal` argument).
4. **Tests:** Update `UnrealAiTodoPlanContractTest` and any harness tests that assert legacy tool behavior; add migration unit tests for JSON load.

---

## 4. Removal checklist (code and assets)

Execute only after migration path is in place and covered by tests.

### 4.1 Context and persistence

- `FAgentContextState::ActiveTodoPlanJson`, `TodoStepsDone`
- `IAgentContextService::SetActiveTodoPlan`, `SetTodoStepDone`, related methods
- `AgentContextJson` fields `activeTodoPlan`, todo step arrays
- `TodoState` branch in `UnrealAiContextCandidates::CollectCandidates`
- `UnrealAiFormatActiveTodoSummary` usage for legacy todo (keep helper only if reused for DAG summaries)
- `FAgentContextBuildResult::ActiveTodoSummaryText` if only serving legacy todo (evaluate whether DAG needs equivalent token)

### 4.2 Tools and dispatch

- `agent_emit_todo_plan` entry in `UnrealAiToolDispatch.cpp` / `UnrealAiToolDispatch_Context.cpp` (`UnrealAiDispatch_AgentEmitTodoPlan`)
- Tool catalog JSON definition and prompt chunks referencing the legacy tool

### 4.3 Harness

- `RecordToolResult` skip / `OnTodoPlanEmitted` / `bTodoPlanOnly` branches tied **only** to legacy tool
- `CompleteToolPath` coarse `TodoStepsDone` advancement loop
- `UnrealAiTurnLlmRequestBuilder` execution subturn gated on `ActiveTodoPlanJson`

### 4.4 UI

- `STodoPlanPanel` / transcript handling for `unreal_ai.todo_plan` **if** no longer needed after UI shows only DAG; otherwise restrict panel to DAG-only JSON
- `FUnrealAiChatTranscript::AddTodoPlan` and `EUnrealAiChatBlockKind::TodoPlan` if superseded by plan run progress only

### 4.5 Prompts

- Remove or replace `{{ACTIVE_TODO_SUMMARY}}` and any chunks that assume legacy todo schema

---

## 5. What stays (non-goals of removal)

- **`FUnrealAiPlanExecutor`** and **`ActivePlanDagJson`** — canonical execution path.
- **Plan / node prompts** under `prompts/chunks/plan/` and `plan-node/`.
- **Thread suffix `_plan_<nodeId>`** and parent-thread context writes (see subagents doc).

---

## 6. Verification

- `./build-editor.ps1 -Headless`
- Plan mode: planner → approval gate → node execution → failure/replan paths
- Agent mode: internal plan does not spam transcript; optional context candidate behavior
- Load old `context.json` with only `activeTodoPlan` and confirm migration or safe fallback
- Context ranking tests: no `TodoState` type; `PlanState` still packs under budget

---

## 7. Relation to context-management refactor

The **context pipeline** work (single candidate gate, ingestion modules, project tree inside packer, **working set**, harness autofill) can ship **before** or **in parallel** with this effort. After legacy removal, **one fewer** candidate type (`TodoState`) is collected; `PlanState` remains. No need to duplicate planning details in the context plan.
