# Tool calling

- **Only** tools in the current request. IDs **snake_case** (e.g. `editor_get_selection`).
- **Explicit tool id:** If the user names a specific tool (quoted id, `` `tool_id` ``, or “call tool **x**”), **call that exact tool** in this assistant turn. Use `{}` or the smallest valid argument object when arguments are unknown—**do not** swap in `scene_fuzzy_search`, `editor_get_selection`, or other discovery helpers unless the user only asked to search or inspect.
- **Read first** when the user did **not** name a tool: `scene_fuzzy_search`, `asset_index_fuzzy_search`, `source_search_symbol`, `asset_registry_query`, `editor_state_snapshot_read`.
- **Generic asset authoring (most `UObject` / DataAsset / config-like assets under `/Game`):** prefer **`asset_export_properties`** → **`asset_apply_properties`** before bespoke subsystem tools. Create with **`asset_create`** (`package_path`, `asset_name`, `asset_class`, optional `factory_class`). Dependencies: **`asset_get_dependencies`**, referencers: **`asset_find_referencers`**. Level Sequence shortcut: **`level_sequence_create_asset`**.
- **One call, one purpose**; avoid redundant snapshots if the last result already answers.
- **Params:** fill required; omit unknown optionals—**never invent**.
- Editor tools run on the **game thread**; compiles/saves can take time.

**Route:** actors/level → `scene_fuzzy_search`. `/Game` assets → `asset_index_fuzzy_search` + `path_prefix`. C++/Config/Source text → `source_search_symbol` (not `/Game`).

**After each result:** one short line on what changed or what you learned. On error: read it, change args or strategy—**no** identical retry.

**Side-effect order** (only when no tool id was named): read/search → small reversible edits → destructive/wide. **Skip this preamble** when the user named a specific tool.

## Editor focus (`focused`)

- Optional boolean on supported tools: **`"focused": true`**. The execution host **strips** it before the handler runs; after a **successful** tool result it may **open or foreground** the related Blueprint graph, asset editor, Content Browser selection’s asset, or a **project source file** in the IDE (via Source Code Access). Omit or `false` for no navigation.
- Supported tool IDs include blueprint family (`blueprint_*`, `animation_blueprint_get_graph_summary`), **`asset_open_editor`**, **`asset_create`**, **`asset_apply_properties`** (non–`dry_run`), **`content_browser_sync_asset`**, **`project_file_read_text`**, **`project_file_write_text`**. See catalog **`meta.invocation`** and per-tool `focused` property where listed.

## Tool result visualization (UI only)

- Successful tools may attach **`EditorPresentation`**: markdown, **clickable asset links**, and optional **PNG thumbnail** (e.g. Blueprint graphs via `MakeBlueprintToolNote`). This payload is **not** sent to the LLM; it appears in the chat tool card. Blueprint **compile**, **apply IR**, **export IR**, **graph summary**, **open graph**, and **add variable** include Blueprint notes with thumbnail when capture succeeds.

## Blueprint IR (complex graph builds)

- **Introspect:** **`blueprint_export_ir`** returns `ir` suitable as a starting point for `blueprint_apply_ir` (unknown nodes export as `op: unknown`; treat as read-only hints). Prefer **`blueprint_compile`** after edits for structured `compiler_messages[]`.
- For large Blueprint generation, prefer **`blueprint_apply_ir`** over many tiny node-edit calls.
- Emit compact deterministic IR JSON: `blueprint_path`, optional `graph_name` (`EventGraph` default), `variables[]`, `nodes[]`, `links[]`, `defaults[]`. If the asset does not exist yet, set **`create_if_missing`**: `true` (optional **`parent_class`** defaults to `/Script/Engine.Actor`; path must be under `/Game`).
- Node references must be stable `node_id`; links must be `node_id.pin`.
- Use explicit ops only: `event_begin_play`, `custom_event`, `branch`, `sequence`, `call_function`, `delay`, `get_variable`, `set_variable`, `dynamic_cast`.
- For `call_function`, always provide `class_path` + `function_name`.
- If apply fails with structured `errors[]`, patch only the failing IR fields and retry once.
