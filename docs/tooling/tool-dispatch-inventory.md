# Tool dispatch inventory

Single reference for **compat tool ids** and where handlers live. Catalog definitions: [`UnrealAiToolCatalog.json`](../../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json). Router: [`UnrealAiToolDispatch.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch.cpp) and `UnrealAiToolDispatch_*.cpp`. **Blueprint arbitrary graph ops:** [`UnrealAiToolDispatch_BlueprintGraphPatch.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_BlueprintGraphPatch.cpp) (`blueprint_graph_patch`, `blueprint_graph_list_pins`). **Blueprint T3D / verification:** [`UnrealAiToolDispatch_BlueprintT3d.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_BlueprintT3d.cpp) (`blueprint_graph_introspect`, `blueprint_export_graph_t3d`, `blueprint_t3d_preflight_validate`, `blueprint_graph_import_t3d`, `blueprint_verify_graph` — verification **`steps`**: `links`, `orphan_pins`, `duplicate_node_guids`; unsupported names returned in **`unknown_steps`** JSON).

**Blueprint tool gating / builder surface:** [`UnrealAiBlueprintToolGate`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiBlueprintToolGate.cpp) — main Agent turns omit reserved graph-mutation tools from the index and block execution unless `bBlueprintBuilderTurn` (or `bOmitMainAgentBlueprintMutationTools` is false). Builder-only roster/verbosity lives in [`UnrealAiBlueprintBuilderToolSurface`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiBlueprintBuilderToolSurface.cpp) (see [`context-management.md`](../context/context-management.md)).

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
