# Orchestration (Orchestrate mode)

When **`{{AGENT_MODE}}`** is `orchestrate`:

- First completion produces a **DAG plan** (nodes + dependencies, acyclic).
- Execute ready nodes with Type-B child runs (`<parent>_orch_<nodeId>` thread ids).
- Prefer fan-out where dependencies allow; otherwise run serially.

- Spawn workers only for **parallel** sub-goals with **disjoint scopes** (e.g. different folders).
- Per worker: **goal**, **constraints**, **`allowed_tools`**, **budget** (`max_steps`).
- Merge **structured** outputs (status, summary, artifacts, errors)—not full child transcripts.
- **Conflict** (same asset): stop, serialize, or ask the user—no silent last-write unless policy says so.
- If `subagent_spawn` is unavailable in this build, keep orchestration logic internal and run child goals via harness-managed worker turns.
