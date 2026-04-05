# Tool reference for prompt authors

- **Canonical catalog (this plugin):** Primary [`../../Resources/tools.main.json`](../../Resources/tools.main.json) (`meta` + main roster); merged with [`../../Resources/tools.blueprint.json`](../../Resources/tools.blueprint.json) and [`../../Resources/tools.environment.json`](../../Resources/tools.environment.json) per **`meta.tool_catalog_fragments`** — full parameters, `modes`, `permission`, `status`, `failure_modes` after merge.
- **Human narrative (repo docs):** [`tool-registry.md`](../../../../docs/tooling/tool-registry.md).
- **Snapshot (regenerate):** [`catalog-snapshot.tsv`](catalog-snapshot.tsv) â€” pipe-separated export for diffing when the JSON changes.
- **Grouped for prompts:** [`by-category.md`](by-category.md) — same data organized by `category` for “tool pack” and routing prose. Keep **`blueprints`** rows aligned with formatter-related tools: **`blueprint_graph_patch`**, **`blueprint_format_graph`** (layout follows **Editor Preferences** / Blueprint toolbar format options; layout metrics on success), **`blueprint_get_graph_summary`** (**`include_layout_analysis`**), **`blueprint_compile`** (`format_graphs`), **`blueprint_graph_introspect`**, matching **`04-tool-calling-contract.md`**.
- **Core pack defaults:** [`core-pack.md`](core-pack.md) â€” tools flagged `always_include_in_core_pack` in JSON.

## Structured planning (product)

Use **Plan mode** in chat for `unreal_ai.plan_dag` (planner JSON + executor). See **`chunks/plan/`**, **`chunks/plan-node/`**, and `Private/Planning/` in source.

Legacy persisted **`activeTodoPlan`** JSON may still exist on disk from older sessions—see [`context-management.md`](../../../../docs/context/context-management.md).
