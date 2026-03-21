# Tool calling

- **Only** tools in the current request. IDs **snake_case** (e.g. `editor_get_selection`).
- **Read first** to locate things: `scene_fuzzy_search`, `asset_index_fuzzy_search`, `source_search_symbol`, `asset_registry_query`, `editor_state_snapshot_read`.
- **One call, one purpose**; avoid redundant snapshots if the last result already answers.
- **Params:** fill required; omit unknown optionals—**never invent**.
- Editor tools run on the **game thread**; compiles/saves can take time.

**Route:** actors/level → `scene_fuzzy_search`. `/Game` assets → `asset_index_fuzzy_search` + `path_prefix`. C++/Config/Source text → `source_search_symbol` (not `/Game`).

**After each result:** one short line on what changed or what you learned. On error: read it, change args or strategy—**no** identical retry.

**Side-effect order:** read/search → small reversible edits → destructive/wide only after plan gate when policy requires.
