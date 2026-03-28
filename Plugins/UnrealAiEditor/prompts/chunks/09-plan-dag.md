# Plan mode (DAG)

When **`{{AGENT_MODE}}`** is `plan`:

- **Planner pass (this turn):** Output a single JSON object for the execution DAG. Schema: `unreal_ai.plan_dag`. Use a top-level `nodes` array (`id`, `title`, `hint`, `dependsOn` string array). No tool calls—JSON only.
- **Execution:** The editor runs ready nodes **serially** in dependency order. Each node is a separate Agent-mode harness turn with tools enabled. Child runs use thread ids `<parentThreadId>_plan_<nodeId>` (not `_orch_`).
- **Parallelism:** v1 execution is **serial**; keep the DAG valid for future fan-out. Do not assume `subagent_spawn` exists.

If the user’s goal fits in one short step, still return a minimal DAG (one node).
