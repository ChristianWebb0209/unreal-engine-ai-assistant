# Plan mode (DAG)

When **`{{AGENT_MODE}}`** is `plan`:

- **Planner pass (this turn):** Output a single JSON object for the execution DAG. Schema: `unreal_ai.plan_dag`. Use a top-level `nodes` array (`id`, `title`, `hint`, `dependsOn` string array). **No tool calls** — do not emit `tool_calls` or native function/tool blocks; assistant message content must be **JSON only** (the harness does not execute tools in this pass).
- **Execution:** The editor runs ready nodes **serially** in dependency order. Each node is a separate Agent-mode harness turn with tools enabled. Child runs use thread ids `<parentThreadId>_plan_<nodeId>` (not `_orch_`).
- **Parallelism:** v1 execution is **serial**; keep the DAG valid for future fan-out. Do not assume parallel child agent runs exist in this plugin.
- **Execution turns:** node `hint` / `title` are prose, not validated paths. Child Agent runs must still pass real tool arguments from discovery/context (**04**)—not from plan text alone.
- **v1 cost:** each node is a **full Agent harness turn** (tools enabled). Too many nodes on a trivial ask increases wall time, tool noise, and harness risk—match node count to user intent (see **Plan** mode in **`02-operating-modes.md`**).

If the user’s goal fits in one short step, still return a minimal DAG (one node).
