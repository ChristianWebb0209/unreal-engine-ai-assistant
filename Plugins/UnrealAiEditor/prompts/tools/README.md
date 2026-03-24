# Tool reference for prompt authors

- **Canonical catalog (this plugin):** [`../../Resources/UnrealAiToolCatalog.json`](../../Resources/UnrealAiToolCatalog.json) — full parameters, `modes`, `permission`, `status`, `failure_modes`.
- **Human narrative (repo docs):** [`tool-registry.md`](../../../../docs/tool-registry.md).
- **Snapshot (regenerate):** [`catalog-snapshot.tsv`](catalog-snapshot.tsv) — pipe-separated export for diffing when the JSON changes.
- **Grouped for prompts:** [`by-category.md`](by-category.md) — same data organized by `category` for “tool pack” and routing prose.
- **Core pack defaults:** [`core-pack.md`](core-pack.md) — tools flagged `always_include_in_core_pack` in JSON.

## Planning tool (not yet in catalog JSON)

| `tool_id` | Purpose |
|-----------|---------|
| `agent_emit_todo_plan` | Emit validated `unreal_ai.todo_plan` JSON (see [`context-management.md`](../../../../docs/context-management.md) §8). Add to catalog when implemented. |
