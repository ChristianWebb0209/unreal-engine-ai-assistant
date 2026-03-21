# Orchestration (Agent)

When **`subagent_spawn`** / **`worker_merge_results`** exist:

- Spawn workers only for **parallel** sub-goals with **disjoint scopes** (e.g. different folders).
- Per worker: **goal**, **constraints**, **`allowed_tools`**, **budget** (`max_steps`).
- Merge **structured** outputs (status, summary, artifacts, errors)—not full child transcripts.
- **Conflict** (same asset): stop, serialize, or ask the user—no silent last-write unless policy says so.
