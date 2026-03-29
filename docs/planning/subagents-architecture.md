# Subagents, parallel plan nodes, and architecture

This document describes **today’s** plan-DAG execution model in the Unreal AI Editor plugin, the **product and safety thinking** behind future **subagents / parallel node execution**, and a **concrete implementation plan** (files, boundaries, settings). It is the single place to align prompts, harness behavior, and UI.

**Related:** Ask / Agent / Plan modes are selected in chat and carried on `FUnrealAiAgentTurnRequest::Mode`. Plan DAG execution is serial today (this doc); todo-plan JSON is a separate legacy path.

---

## 1. What we have today (current architecture)

### 1.1 Plan mode: planner JSON, then serial execution

- **Planner pass (`EUnrealAiAgentMode::Plan`):** The model emits a single JSON object, schema `unreal_ai.plan_dag`, with a `nodes[]` array (`id`, `title`, `hint`, `dependsOn` / `depends_on`). No tools in this pass.
- **Validation:** `UnrealAiPlanDag::ParseDagJson` and `UnrealAiPlanDag::ValidateDag` run before any node executes (duplicate ids, unknown deps, cycles, max node count). Invalid graphs do not run children; the executor may perform a **one-shot planner repair** (`FUnrealAiPlanExecutor`).
- **Execution:** `FUnrealAiPlanExecutor` (`Plugins/UnrealAiEditor/.../Private/Planning/FUnrealAiPlanExecutor.cpp`) walks **ready** nodes in dependency order and runs them **strictly serially**: **one** ready node at a time, each as a normal **Agent** harness turn.
- **Thread isolation (per node):** Child runs use thread ids of the form `<parentThreadId>_plan_<nodeId>` so context and logs stay separable; there is **no** concurrent harness turn for multiple plan nodes today.
- **Harness:** `FUnrealAiAgentHarness` runs one turn at a time; plan pipeline lifetime is tracked (`IsPlanPipelineActive`, idle-abort tuning for headed runs).
- **Prompts:** Chunk `09-plan-dag.md` (planner); chunk `11-plan-node-execution.md` for Agent turns whose thread id contains `_plan_` (anti–tool-loop / checklist discipline).
- **Legacy:** `agent_emit_todo_plan` / `unreal_ai.todo_plan` persistence is **separate** from the plan DAG executor and is **deprecated** (not exposed to the model).

### 1.2 What we do not have yet

- **No parallel scheduling** of multiple ready DAG nodes.
- **No subagent process** (no second OS process dedicated to a worker).
- **No catalog tools** for spawn/merge (prior orchestration tools were removed from the catalog).

In documentation we use **“subagent”** to mean **a delegated harness run** (child Agent turn with its own `threadId` and budget), not necessarily a separate executable.

---

## 2. Goals and constraints (product thinking)

### 2.1 Parallelism is optional; serial is always valid

- It is **acceptable and normal** for the DAG to be a **single spine** (width 1 at every wave) or for the product to **never** fan out.
- **Default behavior remains serial** when in doubt or when policy forbids parallel execution.
- Parallel execution is an **acceleration path**, not a requirement for correctness.

### 2.2 Unreal Editor reality

- The editor exposes **one coherent mutable workspace** (world, selection, asset registry, compile pipeline). “Different folders” does not guarantee independence (shared packages, redirects, global compile/save).
- We are **not** relying on mutexes or fine-grained locking for v1 parallel work; **policy** must keep overlapping mutations rare and failures recoverable (e.g. fall back to serial).

### 2.3 Stringent rules for when parallel / subagent-style runs are allowed

Proposed **all** must hold before scheduling two (or more) plan nodes concurrently:

1. **User/product gate:** Editor setting **`Use subagents`** (`bUseSubagents` in `UUnrealAiEditorSettings`) is **true** (see §5). If false, **always serial**.
2. **Graph gate:** At least **two** node ids are **simultaneously ready** (`GetReadyNodeIds` returns ≥2) and the planner has marked them **eligible** for parallel execution (see §3.2).
3. **Independence gate (heuristic):** Nodes must not declare overlapping **critical scopes** (e.g. same primary asset path, same level/map, or both requiring global compile/PIE in ways we cannot order). Exact rules live in **policy code**, not only in prompts.
4. **Size / worth gate:** Each node must meet a **minimum “worth”** threshold so we do not spawn extra harness work for trivial steps (see §3.3).
5. **Safety fallback:** If any check is ambiguous, **run serially**.

### 2.4 What parallel work is “safer” vs riskier (rough ordering)

1. **Read-only / discovery** work in disjoint scopes (lowest risk; modest speedup).
2. **Writes** to **disjoint persisted assets** with explicit paths and no shared parent operation (medium risk; main candidate for parallel **if** large enough).
3. **World / level / PIE / global compile / save-all** — treat as **serial** unless isolated by design.

### 2.5 Minimum task size (“consequential” work)

Orchestration has overhead (harness, LLM rounds, context). **Tiny** nodes should not trigger parallel fan-out.

Proposed **composite** thresholds (implementation can start simple and tighten):

- **Minimum tool budget** per node (e.g. expected tool rounds or a planner-provided estimate).
- **Minimum scope** (e.g. distinct asset paths touched, or explicit “large” flag from planner).
- **Minimum node hint** semantics: e.g. “orientation / checklist” nodes stay **serial prose-first** per `11-plan-node-execution.md`.

Exact numbers belong in **`UnrealAiWaitTime` / policy** or a dedicated **policy struct** so tests can pin them.

---

## 3. Planner and schema extensions (future)

### 3.1 Optional metadata on nodes (conceptual)

To keep separation of concerns, **validation** should not guess intent from free-text `hint` alone. Future DAG nodes may include optional fields, for example:

- `parallel_group` or `wave_id` — nodes in the same wave may run together **if** policy allows.
- `execution_mode`: `serial` | `parallel_ok` | `read_only`.
- `primary_asset` / `scope_paths[]` — for disjointness checks.
- `estimated_weight` — planner-estimated cost for minimum-size gating.

Schema changes require **prompt updates** (`09-plan-dag.md`), **JSON parsing** in `UnrealAiPlanDag`, and **backward compatibility** (missing fields = conservative serial).

### 3.2 Eligibility module

A dedicated **policy** layer decides “may this ready set run in parallel?” without embedding logic in the executor’s `while` loop.

---

## 4. Implementation plan: modules and files (separation of concerns)

Below is a **target** layout. Names can shift; responsibilities should not.

| Responsibility | Proposed location | Notes |
|----------------|-------------------|--------|
| **Settings** | [`UnrealAiEditorSettings.h`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Public/UnrealAiEditorSettings.h) | `bUseSubagents` (default **true**); persisted in Editor config. |
| **DAG types + parse/validate** | Existing: `Private/Planning/UnrealAiPlanDag.h/.cpp` | Extend when adding optional node metadata. |
| **Serial execution (current)** | Existing: `Private/Planning/FUnrealAiPlanExecutor.h/.cpp` | Keep orchestration readable; branch **serial vs parallel wave** here or delegate. |
| **Parallel eligibility + thresholds** | **New:** `Private/Planning/UnrealAiPlanParallelPolicy.h` (+ `.cpp`) | Pure functions / small class: given `FUnrealAiPlanDag`, ready ids, settings, return `EScheduleDecision` or ordered waves. |
| **Wave scheduler (optional)** | **New:** `Private/Planning/FUnrealAiPlanWaveScheduler.h` (+ `.cpp`) | Given policy output, issue `RunTurn` calls (still **one harness**; may queue work or use async completion tokens). |
| **Harness** | Existing: `FUnrealAiAgentHarness`, scenario runner | Ensure parallel child runs do not break `IsPlanPipelineActive`, idle abort, or cancellation. |
| **Context / status** | Existing: `IAgentContextService` / plan node status maps | Per-node `running/success/failed` must remain consistent under concurrency. |
| **Prompts** | `prompts/chunks/09-plan-dag.md`, new fragment if needed | Planner must output only parallel hints when safe; serial spine remains default story. |
| **Tests** | `UnrealAiEditor` module tests or harness dry runs | Policy unit tests; headed smoke optional. |

**Principle:** `FUnrealAiPlanExecutor` **orchestrates**; **policy** decides; **harness** executes; **DAG** parses.

---

## 5. Editor setting: Use subagents

- **Name (C++):** `bUseSubagents`  
- **Display name:** e.g. **Use subagents (parallel plan nodes)**  
- **Storage:** `UUnrealAiEditorSettings` with `Config = Editor` (same as other plugin settings) — **local to the machine/editor user**, not committed to the repo.  
- **Default:** **true** — product intent is to allow parallel delegation when the implementation ships; users can disable globally if they want deterministic serial behavior.

**Wiring (future):** `FUnrealAiPlanExecutor` (or policy) reads `GetDefault<UUnrealAiEditorSettings>()->bUseSubagents` before scheduling a wave. When **false**, skip parallel branches and keep current serial loop.

---

## 6. Phased rollout (suggested)

1. **Phase 0 (now):** Serial DAG, repair, prompts, harness tuning — **no parallel** scheduling.
2. **Phase 1:** Implement `UnrealAiPlanParallelPolicy` + settings gate; still **execute serially** but log **would_parallelize** in dev builds to validate heuristics.
3. **Phase 2:** **Dual-node** parallel only (max 2 concurrent child runs), read-only or disjoint-write only, strict thresholds.
4. **Phase 3:** Generalize wave width; optional merge/summary step for parent transcript.

---

## 7. Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Flaky overlaps despite “disjoint” paths | Serial fallback; conservative policy; clear tool-iteration log entries. |
| Cost explosion (2× LLM calls) | Minimum size gate; user setting off; cap concurrent children. |
| Harness deadlocks / idle abort | Reuse plan-pipeline idle tuning; ensure `OnPlanHarnessSubTurnComplete` ordering per child. |
| User confusion | UI copy: subagents are **optional**; **linear DAG** is always valid. |

---

## 8. Summary

- **Today:** Plan DAG is **validated** and executed **serially**; thread ids isolate nodes; **no** parallel ready-node scheduling.
- **Tomorrow:** Add **policy-gated** parallel waves behind **`bUseSubagents`**, with **stringent** independence and **minimum size** rules, implemented in **new policy/scheduler files** so `FUnrealAiPlanExecutor` stays maintainable.
- **Product truth:** **Zero subagents**, **no branching**, or **always serial** remain **first-class** outcomes.
