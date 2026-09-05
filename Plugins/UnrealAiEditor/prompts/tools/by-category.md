# Tools by category

Derived from the merged catalog (`tools.main.json` + `tools.blueprint.json`). World placement and environment/PCG tools are **removed** from the product.

## `animation_sequencer`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `animation_blueprint_get_graph_summary` | Summarize AnimBlueprint graphs. | read | implemented | ask=True agent=True |
| `level_sequence_create_asset` | Create a Level Sequence asset under /Game (typed shortcut over asset_create). | write | implemented | ask=False agent=True |
| `sequencer_open` | Open Level Sequence in Sequencer. | write | future | ask=False agent=True |

## `assets_content`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `asset_apply_properties` | Write/apply flat JSON property deltas on the loaded asset UObject under /Game (r | write | implemented | ask=False agent=True |
| `asset_create` | Create a new asset under /Game via IAssetTools (asset_class path + optional fact | write | implemented | ask=False agent=True |
| `asset_delete` | Delete assets. | destructive | implemented | ask=False agent=True |
| `asset_duplicate` | Duplicate asset to new path. | write | implemented | ask=False agent=True |
| `asset_export_properties` | Read/export editable UObject properties under /Game as JSON (reflection). For a  | read | implemented | ask=True agent=True |
| `asset_get_metadata` | Read asset metadata and dependencies summary. | read | implemented | ask=True agent=True |
| `asset_graph_query` | Read Asset Registry graph relations for one asset. Required: relation (reference | read | implemented | ask=True agent=True |
| `asset_import` | Import files via automated import pipeline. | write | future | ask=False agent=True |
| `asset_index_fuzzy_search` | Search the Asset Registry under path_prefix (default /Game). With a non-empty qu | read | implemented | ask=True agent=True |
| `asset_registry_query` | Query Asset Registry for deterministic listing by exact path/class filters. You  | read | implemented | ask=True agent=True |
| `asset_rename` | Rename/move asset and optionally fix references. | destructive | implemented | ask=False agent=True |
| `asset_save_packages` | Persist dirty package changes to disk. Use after compile/edit/mutation passes so | write | implemented | ask=False agent=True |

## `audio_metasounds`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `audio_component_preview` | Preview sound in editor. | write | future | ask=False agent=True |
| `metasound_open_editor` | Open MetaSound source. | write | future | ask=False agent=True |

## `blueprints`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `blueprint_compile` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder; not on defau | write | implemented | ask=False agent=True |
| `blueprint_format_graph` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder). Run in-proc | write | implemented | ask=False agent=True |
| `blueprint_get_graph_summary` | Read-only (main Agent + builder): bounded summary of Blueprint graph(s) for an e | read | implemented | ask=True agent=True |
| `blueprint_graph_introspect` | Read-only: enumerate nodes with node_guid, class, title, and pins (name, directi | read | implemented | ask=False agent=True |
| `blueprint_graph_list_pins` | Read-only: pins[] (name, direction, category, optional default_value). node_ref/ | read | implemented | ask=True agent=True |
| `blueprint_graph_patch` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder; not on defau | write | implemented | ask=False agent=True |
| `blueprint_set_component_default` | Blueprint Builder sub-turn only (not default main Agent). Set one reflected prop | write | implemented | ask=False agent=True |
| `blueprint_verify_graph` | Post-edit verification: optional steps (default ["links"]): links (null/cross-gr | read | implemented | ask=False agent=True |

## `build_packaging`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `cook_content_for_platform` | Cook content for a platform (UAT). | exec | future | ask=False agent=True |
| `package_project` | Full package build. | exec | future | ask=False agent=True |
| `shader_compile_wait` | Wait for shader compile completion. | exec | future | ask=False agent=True |

## `capture_vision`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `render_target_readback_editor` | Capture a UTextureRenderTarget2D to disk. | write | future | ask=False agent=True |
| `viewport_capture` | Capture the editor viewport. Required: capture_kind immediate_png or after_frame | write | implemented | ask=False agent=True |

## `console_exec`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `console_command` | Run a bounded editor console exec via allow-list keys (default). command must be | exec | implemented | ask=False agent=True |

## `diagnostics_logs`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `editor_state_snapshot_read` | Read a deterministic editor/world snapshot (JSON). Use when context may be stale | read | implemented | ask=True agent=True |
| `engine_message_log_read` | Read recent Message Log / log tail. | read | implemented | ask=True agent=True |
| `tool_audit_append` | Append line to tool audit log file. | write | designed | ask=False agent=True |

## `editor_ui_navigation`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `asset_open_editor` | Open an asset in its native editor when you already have an exact object path (f | write | implemented | ask=False agent=True |
| `content_browser_navigate_folder` | Navigate Content Browser to a folder. | write | implemented | ask=False agent=True |
| `content_browser_sync_asset` | Focus the Content Browser on a registered asset by object path (`path` / `object | write | implemented | ask=False agent=True |
| `editor_get_mode` | Read the current active editor mode. | read | implemented | ask=True agent=True |
| `editor_set_mode` | Switch editor mode (Place, Landscape, ... allow-listed). | write | implemented | ask=False agent=True |
| `global_tab_focus` | Bring a registered dock tab to front. | write | implemented | ask=False agent=True |
| `menu_command_invoke` | Execute a pre-registered editor command ID. | exec | implemented | ask=False agent=True |

## `materials_rendering`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `material_get_usage_summary` | List referencers for a material. If not found in the registry, the path may be w | read | implemented | ask=True agent=True |
| `material_graph_compile` | Blueprint Builder material_graph: recompile a base Material after graph edits (U | write | implemented | ask=False agent=True |
| `material_graph_export` | Blueprint Builder material_graph: structured export of base Material expressions | read | implemented | ask=True agent=True |
| `material_graph_patch` | Blueprint Builder material_graph only: batched ops on a base Material — add_expr | write | implemented | ask=False agent=True |
| `material_graph_summarize` | Read-only inventory of a base Material's expression graph (class + editor positi | read | implemented | ask=True agent=True |
| `material_graph_validate` | Blueprint Builder material_graph: list orphan expressions (not reachable from an | read | implemented | ask=True agent=True |
| `material_instance_get_scalar_parameter` | Read one scalar parameter value from a Material Instance. | read | implemented | ask=True agent=True |
| `material_instance_get_vector_parameter` | Read one vector parameter value from a Material Instance. | read | implemented | ask=True agent=True |
| `material_instance_set_parameter` | Set a scalar or vector parameter on a Material Instance (main Agent and builder; | write | implemented | ask=False agent=True |

## `physics_collision`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `collision_trace_editor_world` | Line/sphere trace in editor world (collision, raycast, hit from camera or points | read | future | ask=True agent=True |

## `pie_play`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `pie_start` | Start Play-In-Editor. Optional mode: "viewport" (default, in-process PIE) or "st | exec | implemented | ask=False agent=True |
| `pie_status` | Return PIE / play-session state: playing_in_editor, play_session_request_queued, | read | implemented | ask=True agent=True |
| `pie_stop` | Stop current PIE session started via pie_start. Use when playtest/run requests a | exec | implemented | ask=False agent=True |

## `project_files_search`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `cpp_project_compile` | Compile via Engine Build.bat (Windows). Blocked in normal interactive editor unl | exec | implemented | ask=False agent=True |
| `project_file_move` | Move or rename under the project. Source and destination must both be under Save | destructive | implemented | ask=False agent=True |
| `project_file_read_text` | Read a text file anywhere under the project directory (path relative to project  | read | implemented | ask=True agent=True |
| `project_file_write_text` | Write a text file under the project. Default safe location: Saved/UnrealAiEditor | destructive | future | ask=False agent=True |
| `source_search_symbol` | Fuzzy grep project text files under Source/, Config/, and Plugins/*/Source/ (Cur | read | implemented | ask=True agent=True |

## `properties_settings`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `entity_get_property` | Read a single allowlisted property from an entity (currently actor-focused). Pre | read | implemented | ask=True agent=True |
| `entity_set_property` | Set a single allowlisted property on an entity (currently actor-focused). Prefer | write | implemented | ask=False agent=True |

## `selection_framing`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `editor_get_selection` | Return only the current editor selection (paths and labels); may be empty. Do no | read | implemented | ask=True agent=True |
| `editor_set_selection` | Replace editor selection with level actors only. Paths must be world/level actor | write | implemented | ask=False agent=True |
| `actor_get_material_slots` | List material slots on a selected actor's mesh components (material_path, parameter names). | read | implemented | ask=True agent=True |
| `viewport_frame` | Frame the viewport on selection or explicit actors. Required: target selection o | write | implemented | ask=False agent=True |

## `settings_properties`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `setting_apply` | Write one allowlisted setting using the domain/key settings envelope. Model-faci | write | implemented | ask=False agent=True |
| `setting_query` | Read one allowlisted setting using the domain/key settings envelope. Model-facin | read | implemented | ask=True agent=True |

## `viewport_camera`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `viewport_camera_control` | Unified viewport camera tool. Required: operation (dolly/orbit/pan/pilot/get_tra | write | implemented | ask=True agent=True |
| `viewport_get_view_mode` | Read the current editor viewport rendering mode. | read | implemented | ask=True agent=True |
| `viewport_set_view_mode` | Switch editor viewport rendering mode (Lit, Wireframe, Unlit, …). Does not requi | write | implemented | ask=False agent=True |

## `world_actors`

| tool_id | summary | permission | status | modes |
| --- | --- | --- | --- | --- |
| `actor_find_by_label` | Find actors by outliner label, internal actor name, full object path substring,  | read | implemented | ask=True agent=True |
| `actor_get_transform` | Read actor transform and mobility. | read | implemented | ask=True agent=True |
| `actor_get_visibility` | Read whether an actor is currently hidden in the editor. | read | implemented | ask=True agent=True |
| `scene_fuzzy_search` | Search actors in the loaded editor world/level (not Content Browser assets). For | read | implemented | ask=True agent=True |
