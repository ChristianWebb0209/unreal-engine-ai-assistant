# Tooling follow-ups (structural backlog)

These items came from comparing **`docs/tool-goals.md`** MVP tasks, the **catalog matrix** behavior (empty `{}` args, contract coverage), and real editor workflows. **Not implemented here** — track for future design.

## Gaps vs. MVP gameplay goals

1. **Input / mapping**  
   No first-class tool for Enhanced Input mappings, `DefaultInput.ini`, or project input settings. Workarounds: `project_file_read_text` / `project_file_write_text` on `Config/` (fragile) or ask the user to bind in editor.

2. **UMG / Widget Designer**  
   `asset_create` can create Widget Blueprints; **`blueprint_apply_ir`** targets **EventGraph-style** graphs, not a full Widget hierarchy or designer layout. Agents will struggle with **health bars, minimap UI, HUD** unless we add widget-specific IR, a property-driven layout export/apply, or accept “manual UMG” as out-of-band.

3. **Animation / AnimNotifies**  
   `animation_blueprint_get_graph_summary` exists; **no** symmetric apply tool for anim graphs. Footsteps often need **AnimNotify** in skeletal assets — thin tool coverage.

4. **AI / Navigation**  
   NavMesh building, Behavior Tree, Blackboard: **no** dedicated tools in the current catalog. `console_command` or user-driven **Recast** build may be the only option.

5. **Level Sequence / cinematic**  
   `level_sequence_create_asset` and `sequencer_open` exist; **no** tool to add tracks, bind actors, or keyframes programmatically at scale.

6. **SaveGame**  
   Creating a **SaveGame** Blueprint and wiring `SaveGame`/`LoadGame` is possible via `asset_create` + `blueprint_apply_ir`, but there is no **`save_game_slot` / `load_game_slot`**-style wrapper; agents must know engine APIs in IR.

7. **Physics in editor**  
   `physics_impulse_actor` is **PIE-oriented**; pushing physics objects in **editor** is different from PIE. Clarify in UX when to use which.

8. **Matrix signal vs. success**  
   Catalog matrix invokes many tools with `{}`. **`bOk: false`** with a clear `error` is normal and still **contract_ok**. Avoid training models to treat every `ok:false` as a tooling bug.

## Possible future tools (ideas only)

- Input: `project_input_list_bindings` / `asset_apply` on Input Action assets (if using Enhanced Input assets under `/Game`).
- UMG: `widget_blueprint_export_ir` / `widget_blueprint_apply_ir` (subset) or **layout JSON** on `UWidgetTree`.
- AI: `navmesh_build`, `behavior_tree_open_graph` (read-only at minimum).
- Save: thin wrappers around `UGameplayStatics::SaveGameToSlot` / `LoadGameFromSlot` (requires game module or editor-safe test).

## Possible removals / tightening

- Review **`tier: future`** / **`status: banned`** tools periodically so the model is not offered dead ends.
- **`physics_impulse_actor`** parameter schema (catalog vs dispatch) should stay aligned so agents do not send wrong shapes.

## References

- `docs/tool-goals.md` — qualitative MVP list  
- `tests/TOOL_ITERATION_AGENT_PROMPT.md` — matrix iteration loop  
- `Saved/UnrealAiEditor/Automation/tool_matrix_last.json` — last matrix output  
