# Tool reference for prompt authors

- **Canonical catalog (this plugin):** [`../../Resources/UnrealAiToolCatalog.json`](../../Resources/UnrealAiToolCatalog.json) — full parameters, `modes`, `permission`, `status`, `failure_modes`.
- **Human narrative (repo docs):** [`tool-registry.md`](../../../../docs/tool-registry.md).
- **Snapshot (regenerate):** [`catalog-snapshot.tsv`](catalog-snapshot.tsv) — pipe-separated export for diffing when the JSON changes.
- **Grouped for prompts:** [`by-category.md`](by-category.md) — same data organized by `category` for “tool pack” and routing prose. Keep **`blueprints`** rows aligned with formatter-related tools: **`blueprint_apply_ir`**, **`blueprint_export_ir`**, **`blueprint_format_graph`**, **`blueprint_compile`** (`format_graphs`), matching **`04-tool-calling-contract.md`**.
- **Core pack defaults:** [`core-pack.md`](core-pack.md) — tools flagged `always_include_in_core_pack` in JSON.

## Planning tool

| `tool_id` | Purpose |
|-----------|---------|
| `agent_emit_todo_plan` | **Agent-mode** structured todo plan (`unreal_ai.todo_plan` via tool call). Distinct from **Plan mode** chat/harness DAG (`unreal_ai.plan_dag` assistant JSON + `FUnrealAiPlanExecutor`). See [`planning.md`](../../../../docs/planning.md). |

See [`context-management.md`](../../../../docs/context-management.md) for persistence and §8 where applicable.
