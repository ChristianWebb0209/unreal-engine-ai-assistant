# Unreal MCP — tool calling

## Tool names

MCP tool names are **catalog `tool_id`s** (snake_case), e.g. `editor_get_selection`, `blueprint_graph_patch`. They map 1:1 to the plugin dispatch layer—no `unreal_` prefix.

The active pack is listed in **`06-mcp-tool-pack-index.md`** (generated from `Resources/mcp/mcp-tool-pack.v1.json`). At runtime the MCP server loads schemas from **`GET /v1/tools`** (editor bridge) or falls back to `unreal-mcp/resources/tools.generated.json`.

## Rules

- **Discovery before targeted calls:** Use `asset_index_fuzzy_search`, `scene_fuzzy_search`, or `editor_state_snapshot_read` before tools that require `blueprint_path`, `object_path`, or `actor_path`.
- **Path names:** Assets use `/Game/Folder/Asset.Asset`. Blueprint tools use **`blueprint_path`**. Content Browser sync may use `path` / `object_path` per that tool's schema.
- **Required args:** If the schema lists `required` fields, `{}` is invalid. Use `suggested_correct_call` from errors when present.
- **Selection vs search:** `editor_get_selection` is current selection only—not level-wide discovery.

## Hero flows

**Inspect selection:** `editor_get_selection` → optional `actor_get_transform` (not in pack) / open BP via `asset_open_editor`.

**Find and open asset:** `asset_index_fuzzy_search` → `content_browser_sync_asset` → `asset_open_editor`.

**Blueprint edit:** see **`03-blueprint-loop.md`**.
