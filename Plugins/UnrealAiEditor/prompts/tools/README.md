# Tool reference for prompt authors

- **Canonical catalog (this plugin):** [`../../Resources/UnrealAiToolCatalog.json`](../../Resources/UnrealAiToolCatalog.json) â€” full parameters, `modes`, `permission`, `status`, `failure_modes`.
- **Human narrative (repo docs):** [`tool-registry.md`](../../../../docs/tooling/tool-registry.md).
- **Snapshot (regenerate):** [`catalog-snapshot.tsv`](catalog-snapshot.tsv) â€” pipe-separated export for diffing when the JSON changes.
- **Grouped for prompts:** [`by-category.md`](by-category.md) â€” same data organized by `category` for â€œtool packâ€ and routing prose. Keep **`blueprints`** rows aligned with formatter-related tools: **`blueprint_apply_ir`**, **`blueprint_export_ir`**, **`blueprint_format_graph`**, **`blueprint_compile`** (`format_graphs`), matching **`04-tool-calling-contract.md`**.
- **Core pack defaults:** [`core-pack.md`](core-pack.md) â€” tools flagged `always_include_in_core_pack` in JSON.

## Structured planning (product)

Use **Plan mode** in chat for `unreal_ai.plan_dag` (planner JSON + executor). See chunks **`09-plan-dag.md`**, **`11-plan-node-execution.md`**, and `Private/Planning/` in source.

The catalog entry **`agent_emit_todo_plan`** is **deprecated** (not exposed to the model). Legacy persisted `activeTodoPlan` JSON may still exist on disk—see [`context-management.md`](../../../../docs/context/context-management.md).
