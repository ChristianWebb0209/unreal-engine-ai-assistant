# Tool dispatch inventory

Single reference for **compat tool ids** and where handlers live. Catalog definitions: [`UnrealAiToolCatalog.json`](../../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json). Router: [`UnrealAiToolDispatch.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch.cpp) and `UnrealAiToolDispatch_*.cpp`.

## Compatibility / alias `tool_id`s

| `tool_id` | Behavior |
|-----------|----------|
| `scene_fuzzy_search.query` | Routed to the same handler as `scene_fuzzy_search` (legacy agent naming). |
| `asset_destroy` | Normalized to `asset_delete`; error text directs callers to `asset_delete` with `object_paths` + `confirm`. |

## `console_command`

- **Default:** Allow-list keys only — see [`UnrealAiToolDispatch_Console.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_Console.cpp) and catalog summary for `stat_*`, `r_vsync`, `viewmode_*`.
- **Legacy wide exec:** `UUnrealAiEditorSettings::bConsoleCommandLegacyWideExec` or `UNREAL_AI_CONSOLE_COMMAND_LEGACY_EXEC=1` (see settings header).

## Deprecated catalog entries

Tools marked `status": "deprecated"` in JSON remain callable for backward compatibility where a handler exists; prefer the canonical id in summaries and new prompts.

## First-class tools (Phase E / future)

Prefer new dedicated tools only when the workflow needs **structured parameters** or ranks poorly as a generic console string. New console keys belong in the allow-list table in `UnrealAiToolDispatch_Console.cpp` first.
