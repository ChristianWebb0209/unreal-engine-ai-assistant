# Orchestrate mode — design plan

This document captures the **target** interaction model for Unreal AI Editor: what **Orchestrate** means, how it differs from **Agent**, how **Fast** is retired, and how execution maps to the existing harness (`FUnrealAiAgentHarness`), context service, tool catalog, and model caps (`maxAgentLlmRounds`, etc.).

Related: [`agent-harness.md`](agent-harness.md), [`context-management.md`](context-management.md) §8.4, prompt chunk [`02-operating-modes.md`](../Plugins/UnrealAiEditor/prompts/chunks/02-operating-modes.md), [`09-orchestration-workers.md`](../Plugins/UnrealAiEditor/prompts/chunks/09-orchestration-workers.md).

---

## 1. Naming and goals

**Orchestrate** (user-facing name) is the mode where the product **does not** try to solve the user’s request in one monolithic LLM thread. Instead:

1. A **planning** completion produces a **DAG** (directed acyclic graph) of tasks: ordered work with explicit **dependencies** and **fan-out** points where independent branches can run in parallel.
2. The runtime **executes** that graph: at each step (or batch of steps that fit in the active context window), it runs **tool calls** and/or **sub-agents** (separate LLM sessions with scoped context), then merges results and advances the graph until **done**, **blocked**, **cancelled**, or **limits hit**.

**Why not “plan”?** “Plan” sounds like a static checklist. **Orchestrate** emphasizes **conducting** many workers, **parallelism** at graph branches, and **repeated context assembly**—not a one-off todo paragraph.

---

## 2. Mode matrix: today vs target

### Today (code + prompts)

| Mode   | Intent (current) |
|--------|-------------------|
| **Ask**  | Read-only tools; no mutating work. |
| **Fast** | Default loop: read → act → observe; “smaller” tool surface in catalog. |
| **Agent** | Same spirit as Fast plus **orchestration hooks** when implemented (`subagent_spawn`, worker merge); described as “full orchestration + workers.” |

Composer UI and `EUnrealAiAgentMode` today: `Ask`, `Fast`, `Agent`. Default in `FUnrealAiAgentTurnRequest` is **Fast**.

### Target

| Mode            | Role |
|-----------------|------|
| **Ask**         | Unchanged: read-only, safe exploration. |
| **Agent**       | **Single primary model**, **standard tool loop**: the harness iterates **completion → optional tool calls → tool results → next completion** until the task is finished, the model stops calling tools, or **`maxAgentLlmRounds`** is reached (per model profile in `plugin_settings.json`). This subsumes what **Fast** was for: the default “get it done in one thread” experience. **Fast becomes redundant** and should be **removed from the UI** and eventually from the enum / catalog mode filters (or kept only as an internal alias during migration). |
| **Orchestrate** | **Two-phase (minimum) pipeline**: (A) **DAG planning** completion—structured output only, no implementation; (B) **execution** loop that **spawns subagents** at graph nodes (especially parallel fan-out), **rebuilds context windows** per batch, and respects the same **round/iteration caps** where applicable (parent planner vs per-worker). |

**Summary:** **Fast** → effectively **gone** (behavior folded into **Agent** as the default “one model, tool loop until cap”). **Orchestrate** → **new top tier**: DAG plan + real **subagent** execution and **parallelism** at the DAG.

---

## 3. Agent mode (after Fast retirement)

**Agent** is the **workhorse** mode:

- One **conversation thread** (plus normal `conversation.json` persistence).
- **Tools** enabled per **agent** mode in `UnrealAiToolCatalog.json` (today `modes.agent` vs `modes.fast`—target: a **single** “agent” tool list, or `fast` removed and defaults merged into `agent`).
- **Iteration bound:** `FUnrealAiModelCapabilities::MaxAgentLlmRounds` (`maxAgentLlmRounds` in settings)—each **round** is one model completion (possibly emitting tool calls), then tool execution, then the next round.
- **Stopping:** user **Stop**, model returns final answer without tools, unrecoverable error, or **round cap**.

**Prompting:** chunk `02-operating-modes.md` should describe **Agent** as the default **closed loop** with tools—not “smaller than something else” (since Fast is gone).

---

## 4. Orchestrate mode — conceptual pipeline

### Phase 0 — DAG plan (structure only)

**Input:** User message + compact editor/context summary + tool **names/capabilities overview** (not full execution yet).

**System/planner instructions (intent):**

- You are **not** implementing the request in this call.
- Output a **valid, machine-readable DAG** (see schema below) whose nodes are **tasks** (tool batches, searches, sub-questions) with **edges = dependencies**.
- **Prefer parallel structure:** where work is independent, use **multiple ready nodes** at the same depth so the runtime can schedule **many subagents or parallel tool batches** at once.
- Avoid cycles; keep node payloads **bounded** (short titles, explicit tool ids where known).

**Output:** Parsed DAG stored in **durable state** (extend or align with `ActiveTodoPlanJson` / a new `orchestrate_graph.json` field in context—TBD in implementation). The **chat UI** shows a high-level graph or phase banner, not only raw JSON.

### Phase 1+ — Execute until the DAG is complete

**Outer loop (orchestrator):**

1. **Select ready nodes:** nodes whose **dependencies are satisfied** and not yet completed.
2. **Pack a batch** into the **current context budget** (tokens): include a **short rolling summary** of finished nodes, **failure flags**, and the **payload** for each selected node (sub-prompt + allowed tool subset).
3. **Execute:**
   - **Single-node serial:** one subagent = one `RunTurn`-like invocation with a **derived thread id** (pattern already used for workers in `FUnrealAiWorkerOrchestrator`).
   - **Parallel fan-out:** at points with **multiple independent ready nodes**, run **N subagents in parallel** (HTTP in flight concurrently where safe; **tool execution** still serialized on the game thread per Unreal rules).
4. **Merge:** write results back to **shared context** (and parent summary), mark nodes **done/failed/skipped**, update **ready set**.
5. **Repeat** until no ready nodes, deadlock (bug), user cancel, or **parent/child round limits**.

**Context windows:** After each batch, **rebuild** what goes into the next prompt: not a full dump of all prior completions—**summaries**, **artifact pointers** (paths, asset ids), and **active DAG state**.

### Subagents

A **subagent** is a **scoped** harness run:

- **Own thread id** (e.g. `<parent_thread>_orch_<nodeId>`).
- **Narrow tool allowlist** and **tighter instructions** from the node payload.
- **Cap** on its own `maxAgentLlmRounds` (could be lower than the parent’s).
- **Returns** a structured **worker result** (status, summary, errors, artifacts)—aligned with `FUnrealAiWorkerResult` / merge path in [`agent-harness.md`](agent-harness.md).

True **parallel** subagents are the differentiator from plain **Agent** mode (single thread).

---

## 5. DAG schema (sketch for implementation)

Exact JSON can evolve; shape should support parsing and UI:

```json
{
  "schema": "unreal_ai.orchestrate_dag",
  "version": 1,
  "title": "Short run title",
  "nodes": [
    {
      "id": "n1",
      "title": "Discover relevant assets",
      "kind": "tool_batch",
      "depends_on": [],
      "parallel_group": "wave_a",
      "hint": "Optional natural language for subagent"
    }
  ],
  "edges": [
    { "from": "n1", "to": "n2" }
  ]
}
```

- **`depends_on`** or **`edges`** — pick one canonical form; edges are more general for fan-in.
- **`parallel_group`** optional hint for UX; **readiness** is computed from **dependencies**, not from the group name.
- **`kind`** — e.g. `tool_batch`, `llm_only`, `subagent`, `merge` (implementation detail).

Validation: **acyclicity**, **no orphan ids**, **reasonable max nodes** (configurable).

---

## 6. Relationship to existing features

| Feature | Relation to Orchestrate |
|---------|-------------------------|
| **`agent_emit_todo_plan` / `unreal_ai.todo_plan`** | Today: linear-ish plan UI. **Orchestrate** may **supersede** for complex runs or **compile** a DAG to a simplified todo for display only. |
| **`FUnrealAiWorkerOrchestrator`** | **Sequential** workers today; Orchestrate extends to **parallel** fan-out + **DAG scheduling**. |
| **`maxAgentLlmRounds`** | Applies to **Agent** thread; Orchestrate needs **per-layer** limits (planner call + per-subagent + optional parent “supervisor” rounds). |
| **Tool catalog `modes.*`** | Add **`orchestrate`** (or map planner vs executor tool lists separately). **Remove `fast`** once Agent absorbs it. |
| **`EUnrealAiAgentMode`** | Add **`Orchestrate`**, remove **`Fast`** (after migration). |
| **Composer mode dropdown** | **Ask / Agent / Orchestrate**; default **Agent**. |

---

## 7. UX notes

- **Agent:** Feels like “one smart assistant with tools” until cap or done.
- **Orchestrate:** First milestone is **visible structure** (DAG or phased banner), then **progress** on nodes; **parallel** activity should be surfaced (e.g. “Running 3 workers…”).
- **Stop** cancels **parent** and should **signal children** to cancel best-effort.

---

## 8. Implementation milestones (suggested order)

1. **Docs + enum:** Add `Orchestrate` to `EUnrealAiAgentMode`; mark **Fast** deprecated in code comments; update `02-operating-modes.md` and tool catalog schema.
2. **Retire Fast in UI:** Composer shows Ask / Agent / Orchestrate; default Agent; map saved prefs from Fast → Agent once.
3. **DAG planner prompt + parser:** Strict schema validation; persist graph on context state.
4. **Serial DAG executor:** Topological order, one subagent per node (no parallel yet)—proves merge + context rebuild.
5. **Parallel fan-out:** Ready-set batching, concurrent transports, game-thread tool queue discipline.
6. **Limits & telemetry:** Per-node and parent caps; structured logging for debugging partial graphs.

---

## 8.1 Implementation status / harness limits (v1)

- `Ask / Agent / Orchestrate` is now the mode set in UI and prompts; Fast is removed from mode selection and treated as migration-only catalog compatibility.
- Orchestrate v1 uses a **single-runner harness-safe path**: planner turn -> DAG validation -> serial Type-B child turns (`<parent>_orch_<nodeId>` thread ids).
- Parent transcript is intentionally collapsed to coordinator updates per node, instead of forwarding full child delta streams.
- Stop semantics are v1-safe: cancel current in-flight run and prevent further nodes from being scheduled.
- True parallel DAG fan-out remains deferred until harness + transport support concurrent active runs without global `CancelTurn` conflicts.

---

## 9. Open questions

- **Planner model:** Same profile as executor or a **cheaper/faster** model for DAG-only calls?
- **Summarization:** Template-based vs **LLM summary** between waves (cost vs quality).
- **Failure policy:** Retry node, skip branch, or escalate to user when a **critical path** node fails.
- **Security:** Subagent tool allowlists must be **strict**—no privilege escalation via prompt injection from parent.

---

## 10. One-line pitch

**Orchestrate** = *first completion draws the DAG; the engine runs it—parallel where the graph allows—with subagents and fresh context windows until the graph is complete or limits stop the run.* **Agent** = *one model, one thread, tool loop until done or `maxAgentLlmRounds`.* **Fast** = *retired; its job is now Agent’s default.*
