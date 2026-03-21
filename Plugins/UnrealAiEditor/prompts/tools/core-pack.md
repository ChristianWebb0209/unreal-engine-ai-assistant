# Core tool pack (catalog hint)

These tools have **`context_selector.always_include_in_core_pack`: true** in `UnrealAiToolCatalog.json` — useful default allowlist seeds for routing prompts.

| tool_id | category |
|---------|----------|
| `asset_open_editor` | editor_ui_navigation |
| `content_browser_sync_asset` | editor_ui_navigation |
| `editor_get_selection` | selection_framing |
| `editor_set_selection` | selection_framing |
| `editor_state_snapshot_read` | diagnostics_logs |
| `scene_fuzzy_search` | world_actors |
| `asset_index_fuzzy_search` | assets_content |
| `tool_audit_append` | diagnostics_logs |
| `viewport_camera_dolly` | viewport_camera |
| `viewport_camera_orbit` | viewport_camera |
| `viewport_camera_pan` | viewport_camera |
| `viewport_capture_png` | capture_vision |
| `viewport_frame_actors` | selection_framing |

**Note:** Harnesses may still filter by **mode** (`ask` / `fast` / `agent`) and **tier**; banned tools must never be exposed regardless of this list.
