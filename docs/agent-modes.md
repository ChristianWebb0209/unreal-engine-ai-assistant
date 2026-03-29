# Agent modes (Ask, Agent, Plan)

This document is the **canonical overview** of the three chat modes in Unreal AI Editor: what each mode is for, how the harness behaves, and **which source files** implement that behavior. Modes are selected in the Agent Chat UI and carried on `FUnrealAiAgentTurnRequest::Mode` (`EUnrealAiAgentMode` in `AgentContextTypes.h`).

---

## Quick comparison

| Mode | Tools in LLM request | Typical user goal | Terminal assistant output |
|------|---------------------|-------------------|---------------------------|
| **Ask** | **No** mutating tool loop (read-only / no `unreal_ai_dispatch` tool surface as configured) | Answer questions, explain, review proposals without changing the project | Plain assistant text |
| **Agent** | **Yes** — full tool loop, streaming tool calls, execution host | Edit assets, scene, Blueprints, run diagnostics | Text + tool results; optional **`agent_emit_todo_plan`** for `unreal_ai.todo_plan` |
| **Plan** | **Planner pass:** tools list empty / non-actionable tool stream; **Node passes:** Agent-mode turns per DAG node | Produce **`unreal_ai.plan_dag` JSON**, then execute nodes serially | Planner: JSON only; each node: normal agent turn |

---

## Ask (`EUnrealAiAgentMode::Ask`)

**Purpose:** Safe Q&A and analysis without driving mutating editor tools through the agent loop (per product defaults and prompt assembly).

**Behavior (high level):**

- Prompt builder injects the **Ask** slice from `prompts/chunks/02-operating-modes.md`.
- `UnrealAiTurnLlmRequestBuilder` builds requests with **no tool execution surface** (or read-only posture — see tool surface pipeline and settings).
- **No** `FUnrealAiPlanExecutor` — single harness `RunTurn` per send.

**Important files / areas**

| Area | Path (under `Plugins/UnrealAiEditor/`) |
|------|----------------------------------------|
| Mode enum | `Source/UnrealAiEditor/Private/Context/AgentContextTypes.h` |
| Prompt mode slice | `prompts/chunks/02-operating-modes.md` |
| Request assembly | `Private/Harness/UnrealAiTurnLlmRequestBuilder.cpp`, `Private/Prompt/UnrealAiPromptBuilder.cpp` |
| Harness turn loop | `Private/Harness/FUnrealAiAgentHarness.cpp` |
| Chat UI mode | `Private/Widgets/SChatComposer.cpp` |

---

## Agent (`EUnrealAiAgentMode::Agent`)

**Purpose:** Full **agent harness** with tools — the default for “do work in the project.”

**Behavior (high level):**

- LLM may emit **streaming tool calls**; `FUnrealAiAgentHarness` merges deltas, dispatches through `IToolExecutionHost` → `UnrealAiToolDispatch*.cpp`.
- **`agent_emit_todo_plan`** persists **`unreal_ai.todo_plan`** via `IAgentContextService` (not the Plan-mode DAG).
- Bounded **LLM rounds** and token budget from the model profile (`MaxAgentLlmRounds`, capped by harness backstop, typically **512** max).

**Important files / areas**

| Area | Path |
|------|------|
| Harness loop, rounds, idle/tool guards | `Private/Harness/FUnrealAiAgentHarness.cpp` |
| Tool dispatch | `Private/Tools/UnrealAiToolDispatch.cpp` + `UnrealAiToolDispatch_*.cpp` |
| Catalog | `Resources/UnrealAiToolCatalog.json` |
| Todo plan tool / context | `Private/Tools/UnrealAiToolDispatch_Context.cpp`, `Private/Context/FUnrealAiContextService.cpp` |
| Summaries for prompts | `Private/Planning/UnrealAiStructuredPlanSummary.cpp` (todo + DAG one-liners) |

---

## Plan (`EUnrealAiAgentMode::Plan`)

**Purpose:** Two-stage **plan DAG** workflow: (1) **planner** turn outputs JSON only (`unreal_ai.plan_dag`); (2) **`FUnrealAiPlanExecutor`** runs **ready** DAG nodes **serially** as separate **Agent**-mode harness turns (tools enabled per node).

**Behavior (high level):**

1. **Planner turn:** `Mode == Plan` — harness caps planner LLM rounds (`UnrealAiWaitTime::PlannerMaxLlmRounds`), treats **Finish** as assistant-only (no tool execution), parses JSON with `UnrealAiPlanDag::ParseDagJson` / `ValidateDag`.
2. **Executor:** `Private/Planning/FUnrealAiPlanExecutor.cpp` — `BeginPlannerTurn` → `OnPlannerFinished` → `BeginNextReadyNode` for each node.
3. **Each node:** `ChildReq.Mode = Agent`, `ThreadId = <parent>_plan_<nodeId>`, `LlmRoundBudgetFloor` raised so the node is not cut off at the default round cap.

### Plan mode: “Build” pause (chat) vs auto-run (harness)

There are **two entry paths** with **different defaults** for `FUnrealAiPlanExecutorStartOptions::bPauseAfterPlannerForBuild`:

| Path | `bPauseAfterPlannerForBuild` | What happens after a valid DAG |
|------|------------------------------|--------------------------------|
| **Agent Chat UI** (`SChatComposer`) | **`true`** | Executor sets `bAwaitingBuild`, shows draft DAG, and **waits** until the user clicks **Build** → `ApplyDagJsonForBuild` + `ResumeNodeExecution()`. Nodes do **not** run until then. |
| **Headed harness / long-running** (`UnrealAiHarnessScenarioRunner`) | **`false`** (default) | After validation, executor prints “Plan ready: N nodes…” and **immediately** calls `BeginNextReadyNode` — **no** Build click. Same full pipeline as chat, minus the pause. |

**Why chat pauses:** lets the user **edit** the JSON (or cancel) before any mutating node runs.

**Failure modes introduced only by the Build pause (chat):** user never clicks Build (looks “stuck”); invalid JSON after edit; “plan already running” if Build is clicked at the wrong time; validation errors from `ApplyDagJsonForBuild`. These do **not** apply to headed **plan-mode-smoke** runs, which auto-continue.

**Debugging parity:** to match harness behavior inside the editor, you’d add an optional **developer** toggle or environment variable so `SChatComposer` can pass **`bPauseAfterPlannerForBuild = false`** (auto-run nodes like the batch). This does **not** fix **idle-abort** or **SSE completion** issues; it only removes Build-related confusion when comparing UI vs smoke.

**Harness-only shortcut:** set env **`UNREAL_AI_HEADED_PLAN_HARNESS_PLANNER_ONLY=1`** to run **planner + validate only** (`bHarnessPlannerOnlyNoExecute`), with **no** node execution — useful for isolating planner JSON without node/tool/idle noise.

**Important files / areas**

| Area | Path |
|------|------|
| DAG parse / validate / ready set | `Private/Planning/UnrealAiPlanDag.cpp` |
| Plan executor | `Private/Planning/FUnrealAiPlanExecutor.cpp` |
| Planner-only harness rules (empty nudge, Finish handling) | `Private/Planning/UnrealAiPlanPlannerHarness.*`, `FUnrealAiAgentHarness.cpp` |
| UI: start / resume plan | `Private/Widgets/SChatComposer.cpp` |
| Headed harness / batch | `Private/Harness/UnrealAiHarnessScenarioRunner.cpp` |

**Related doc:** [`planning.md`](planning.md) (DAG vs todo plan, persistence fields, smoke tests).

---

## Headed harness sync / idle abort (all modes that use the scenario runner)

Long-running **headed** batches wait on a sync event. If the stream goes **idle** longer than `HarnessSyncIdleAbortMs` (**3000** ms in `UnrealAiWaitTimePolicy.h`) while the turn is still non-terminal, the runner may **cancel** the turn (`TryHarnessIdleAbort` in `UnrealAiHarnessScenarioRunner.cpp`). Special cases:

- **Awaiting first assistant token** while HTTP is active: headed scenario skips abort so slow TTFB does not trip the 3 s window.
- **Post-token stall:** if the model stopped sending deltas but the harness never reached a terminal `run_finished` success path within the idle window, you still get **sync idle abort** — this is independent of the “512” continuation quirk below.

---

## Run.jsonl: `total_phases_hint` and the “512” value

`IAgentRunSink::OnRunContinuation(PhaseIndex, TotalPhasesHint)` is used for **two different meanings**:

1. **Plan executor (parent):** `TotalPhasesHint` = **1 + number of DAG nodes** (planner + nodes).
2. **Agent harness (per LLM round):** `TotalPhasesHint` = **`EffectiveMaxLlmRounds`** (profile cap, up to **512**).

When a **plan node** runs, the child harness emits **(2)**. Those events must **not** be forwarded to the parent sink as “plan phases,” or logs show misleading `total_phases_hint: 512`. The plan executor’s collecting sink **suppresses** forwarding of child continuations during **node** execution so only plan-level phase hints appear.

---

## See also

- [`context-management.md`](context/context-management.md) — context service vs harness ownership.
- [`tool-registry.md`](tooling/tool-registry.md) — tool catalog narrative.
- [`planning.md`](planning.md) — Plan DAG vs `agent_emit_todo_plan`, orchestration notes.
