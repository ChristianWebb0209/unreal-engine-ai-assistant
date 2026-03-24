# Tool calling

- **Only** tools in the current request. IDs **snake_case** (e.g. `editor_get_selection`).
- **Read first** to locate things: `scene_fuzzy_search`, `asset_index_fuzzy_search`, `source_search_symbol`, `asset_registry_query`, `editor_state_snapshot_read`.
- **Generic asset authoring (most `UObject` / DataAsset / config-like assets under `/Game`):** prefer **`asset_export_properties`** → **`asset_apply_properties`** before bespoke subsystem tools. Create with **`asset_create`** (`package_path`, `asset_name`, `asset_class`, optional `factory_class`). Dependencies: **`asset_get_dependencies`**, referencers: **`asset_find_referencers`**. Level Sequence shortcut: **`level_sequence_create_asset`**.
- **One call, one purpose**; avoid redundant snapshots if the last result already answers.
- **Params:** fill required; omit unknown optionals—**never invent**.
- Editor tools run on the **game thread**; compiles/saves can take time.

**Route:** actors/level → `scene_fuzzy_search`. `/Game` assets → `asset_index_fuzzy_search` + `path_prefix`. C++/Config/Source text → `source_search_symbol` (not `/Game`).

**After each result:** one short line on what changed or what you learned. On error: read it, change args or strategy—**no** identical retry.

**Side-effect order:** read/search → small reversible edits → destructive/wide only after plan gate when policy requires.

## Blueprint IR (complex graph builds)

- **Introspect:** **`blueprint_export_ir`** returns `ir` suitable as a starting point for `blueprint_apply_ir` (unknown nodes export as `op: unknown`; treat as read-only hints). Prefer **`blueprint_compile`** after edits for structured `compiler_messages[]`.
- For large Blueprint generation, prefer **`blueprint_apply_ir`** over many tiny node-edit calls.
- Emit compact deterministic IR JSON: `blueprint_path`, optional `graph_name` (`EventGraph` default), `variables[]`, `nodes[]`, `links[]`, `defaults[]`.
- Node references must be stable `node_id`; links must be `node_id.pin`.
- Use explicit ops only: `event_begin_play`, `custom_event`, `branch`, `sequence`, `call_function`, `delay`, `get_variable`, `set_variable`, `dynamic_cast`.
- For `call_function`, always provide `class_path` + `function_name`.
- If apply fails with structured `errors[]`, patch only the failing IR fields and retry once.
