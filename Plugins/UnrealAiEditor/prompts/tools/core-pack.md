# Core tool pack (catalog hint)

These tools have **`context_selector.always_include_in_core_pack`: true** in `tools.main.json` (merged catalog) — useful default allowlist seeds for routing prompts.

| tool_id | category |
|---------|----------|
| `asset_open_editor` | editor_ui_navigation |
| `content_browser_sync_asset` | editor_ui_navigation |
| `editor_get_selection` | selection_framing |
| `editor_set_selection` | selection_framing |
| `scene_fuzzy_search` | world_actors |
| `asset_index_fuzzy_search` | assets_content |
| `tool_audit_append` | diagnostics_logs |
| `viewport_camera_control` | viewport_camera |
| `viewport_capture` | capture_vision |
| `viewport_frame` | selection_framing |

**Note:** Harnesses may still filter by **mode** (`ask` / `agent` / `plan`) and **tier**; banned tools must never be exposed regardless of this list.
