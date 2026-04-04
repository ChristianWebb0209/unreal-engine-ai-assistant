# Plan mode — policies and canonical JSON shape

## DAG size (system rule)

Prefer the **smallest** graph that fits the request. **One node** is correct when a single Agent turn can deliver the outcome (including “give me a short checklist in prose”). **Two nodes** are appropriate only when there are **two clearly separable** outcomes (e.g. “summarize constraints” then “list next actions”)—**not** to look thorough. **Do not** add extra nodes to pad scope; **do not** expand a simple orientation or checklist ask into many steps. The harness **rejects** planner-emitted DAGs that exceed a **small** maximum node count (on the order of **8**); stay minimal so validation always passes.

## Node text quality

Every `hint` must be **traceable** to the user’s wording—no filler “verify / confirm / sanity-check” nodes unless they asked for that kind of check. Prefer **one node** for pure checklist or orientation asks.

## Orientation and checklist asks

If the user asks for **orientation**, a **checklist**, or **“what should I verify first”** without asking you to **inspect or change** the project, treat it as **low surface area**: usually **one node** whose `hint` orders the executor to reply with a **short ordered checklist in plain prose** and to **not** run project searches, asset index queries, or mutating tools unless the user **explicitly** asked for grounded verification of this project.

## Harness fields

For the planner pass: output **only** a **single JSON object** (no prose, no markdown/code fences). The harness parses fields **`nodes`** (preferred) or legacy **`steps`**.

## Canonical shape (use this unless constrained)

```json
{
  "schema": "unreal_ai.plan_dag",
  "title": "Short plan title",
  "nodes": [
    { "id": "a", "title": "Task A", "hint": "What to do", "depends_on": [] },
    { "id": "b", "title": "Task B", "hint": "Follow-up", "depends_on": ["a"] }
  ]
}
```

Do **not** emit `definitionOfDone` / `assumptions` / `risks` wrappers for plan—only the DAG object above (or the same graph using `"steps"` entries with `id`, `title`, `detail`, `dependsOn`).

## Execution model

Prefer plans with **independent branches** so the graph stays easy to extend later; **v1** still runs ready nodes **one at a time** in dependency order. Execution uses **separate harness turns** per ready node (`<parentThreadId>_plan_<nodeId>`); there is no separate worker-merge tool in this plugin. If a node fails, later nodes may not run until status is repaired—keep the DAG small and dependencies explicit.

## Draft + Build (editor)

After the planner pass, the user may **edit the DAG JSON** in the chat UI or send a **follow-up message** describing changes. Treat the **current active plan** (including any user edits) as authoritative: on follow-ups, **merge or replace** nodes to satisfy the new request—do not ignore the existing graph. Respect manual JSON edits over your prior planner output when they conflict.
