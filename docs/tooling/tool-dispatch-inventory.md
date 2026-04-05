# Tool dispatch inventory

Single reference for **compat tool ids** and where handlers live. Catalog definitions: [`tools.main.json`](../../Plugins/UnrealAiEditor/Resources/tools.main.json) (merged with blueprint/environment fragments). Router: [`UnrealAiToolDispatch.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch.cpp) and `UnrealAiToolDispatch_*.cpp`. **Blueprint Kismet mutations:** [`UnrealAiToolDispatch_BlueprintGraphPatch.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_BlueprintGraphPatch.cpp) (`blueprint_graph_patch`, `blueprint_graph_list_pins`). **Blueprint read / verify:** [`UnrealAiToolDispatch_BlueprintT3d.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_BlueprintT3d.cpp) (`blueprint_graph_introspect`; `blueprint_verify_graph` — **`steps`**: `links`, `orphan_pins`, `duplicate_node_guids`, etc.; unsupported names in **`unknown_steps`**). **Family tools** (`setting_query`, `viewport_camera_control`, `viewport_capture`, `viewport_frame`, `asset_graph_query`, `material_instance_set_parameter`): resolver validates the canonical schema, projects args for dispatch, and the router calls the same internal UE handlers as before. **Removed ids** (IR/T3D splits, legacy settings/viewport entry points, `agent_emit_todo_plan`, …): no router branch — `not_implemented` for stale clients.

**Agent tool gating / builder surfaces:** [`UnrealAiAgentToolGate`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiAgentToolGate.cpp) — main Agent turns omit tools whose `agent_surfaces` exclude `main_agent` from the tiered index and block execution unless the matching builder turn is active (`bBlueprintBuilderTurn` / `bEnvironmentBuilderTurn`) or `bOmitMainAgentBlueprintMutationTools` is false. Blueprint roster/verbosity: [`UnrealAiBlueprintBuilderToolSurface`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiBlueprintBuilderToolSurface.cpp). Environment/PCG roster/verbosity: [`UnrealAiEnvironmentBuilderToolSurface`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiEnvironmentBuilderToolSurface.cpp). See [`context-management.md`](../context/context-management.md).

## Compatibility / alias `tool_id`s

| `tool_id` | Behavior |
|-----------|----------|
| `scene_fuzzy_search.query` | Routed to the same handler as `scene_fuzzy_search` (legacy agent naming). |
| `asset_destroy` | Normalized to `asset_delete`; error text directs callers to `asset_delete` with `object_paths` + `confirm`. |

## `console_command`

- **Default:** Allow-list keys only — see [`UnrealAiToolDispatch_Console.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_Console.cpp) and catalog summary for `stat_*`, `r_vsync`, `viewmode_*`.
- **Legacy wide exec:** `UUnrealAiEditorSettings::bConsoleCommandLegacyWideExec` or `UNREAL_AI_CONSOLE_COMMAND_LEGACY_EXEC=1` (see settings header).

## Removed / unknown `tool_id`

Ids that are not in the merged catalog and have no compat alias in `UnrealAiToolDispatch.cpp` fall through to **`status: not_implemented`**. Prefer the canonical family tools and `blueprint_graph_introspect` / `blueprint_graph_patch` for Blueprint graph work.

## First-class tools (Phase E / future)

Prefer new dedicated tools only when the workflow needs **structured parameters** or ranks poorly as a generic console string. New console keys belong in the allow-list table in `UnrealAiToolDispatch_Console.cpp` first.
