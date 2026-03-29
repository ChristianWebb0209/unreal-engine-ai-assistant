# Tool catalog by category (prompt-facing)

Derived from `UnrealAiToolCatalog.json`. Table cells list **ask** and **agent**; **`plan`** appears only in the JSON (`modes.plan` per tool).

## `animation_sequencer`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `animation_blueprint_get_graph_summary` | Summarize AnimBlueprint graphs. | read | future | ask=True agent=True |
| `sequencer_open` | Open Level Sequence in Sequencer. | write | future | ask=False agent=True |

## `assets_content`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `asset_delete` | Delete assets. | destructive | research | ask=False agent=True |
| `asset_duplicate` | Duplicate asset to new path. | write | research | ask=False agent=True |
| `asset_get_metadata` | Read asset metadata and dependencies summary. | read | research | ask=True agent=True |
| `asset_import` | Import files via automated import pipeline. | write | future | ask=False agent=True |
| `asset_index_fuzzy_search` | Fuzzy-search the Unreal Asset Registry index (engine-maintained indexed view of content packages). Uses the same asset data the Content Browser uses; supply path_prefix (e.g. /Game or /Game/Textures) to scope. Ranks asset names, object paths, and class paths even when spelling is approximate. Prefer this over source_search_symbol for textures, materials, meshes, and other UAssets. | read | implemented | ask=True agent=True |
| `asset_registry_query` | Query Asset Registry for assets. | read | research | ask=True agent=True |
| `asset_rename` | Rename/move asset and optionally fix references. | destructive | research | ask=False agent=True |
| `asset_save_packages` | Save dirty packages. | write | research | ask=False agent=True |

## `audio_metasounds`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `audio_component_preview` | Preview sound in editor. | write | future | ask=False agent=True |
| `metasound_open_editor` | Open MetaSound source. | write | future | ask=False agent=True |

## `banned`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `arbitrary_network_fetch` | Banned: generic network fetch. | exec | banned_v1 | ask=False agent=True |
| `arbitrary_process_spawn` | Banned: spawn arbitrary OS processes. | exec | banned_v1 | ask=False agent=True |
| `arbitrary_python_eval` | Banned: arbitrary Python execution. | exec | banned_v1 | ask=False agent=True |
| `delete_system_files` | Banned: paths outside project scope. | destructive | banned_v1 | ask=False agent=True |
| `raw_user_exec_string` | Banned: raw exec/console string. | exec | banned_v1 | ask=False agent=True |

## `blueprints`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `blueprint_add_variable` | Add member variable to Blueprint. | write | future | ask=False agent=True |
| `blueprint_apply_ir` | Apply compact Blueprint IR and materialize/wire EventGraph nodes; merge_policy, event_tick, auto_layout, layout_scope; pair with blueprint_compile. | write | research | ask=False agent=True |
| `blueprint_compile` | Compile a Blueprint and return diagnostics; optional format_graphs runs Unreal Blueprint Formatter on script graphs before compile. | write | research | ask=False agent=True |
| `blueprint_export_ir` | Serialize graph to blueprint_apply_ir-style JSON (lossy for unknown nodes). | read | implemented | ask=True agent=True |
| `blueprint_format_graph` | LayoutEntireGraph readability pass on a script graph; requires UnrealBlueprintFormatter. | write | research | ask=False agent=True |
| `blueprint_get_graph_summary` | Export bounded summary of a Blueprint graph. | read | research | ask=True agent=True |
| `blueprint_open_graph_tab` | Open Blueprint editor focused on a graph. | write | research | ask=False agent=True |

## `build_packaging`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `cook_content_for_platform` | Cook content for a platform (UAT). | exec | future | ask=False agent=True |
| `package_project` | Full package build. | exec | future | ask=False agent=True |
| `shader_compile_wait` | Wait for shader compile completion. | exec | future | ask=False agent=True |

## `capture_vision`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `render_target_readback_editor` | Capture a UTextureRenderTarget2D to disk. | write | future | ask=False agent=True |
| `viewport_capture_delayed` | Schedule viewport capture after N frames. | write | research | ask=False agent=True |
| `viewport_capture_png` | Capture active editor viewport to PNG on disk. | write | research | ask=False agent=True |

## `console_exec`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `console_command` | Execute allow-listed console command. | exec | research | ask=False agent=True |

## `diagnostics_logs`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `editor_state_snapshot_read` | Deterministic editor/world snapshot JSON. | read | research | ask=True agent=True |
| `engine_message_log_read` | Read recent Message Log / log tail. | read | research | ask=True agent=True |
| `tool_audit_append` | Append line to tool audit log file. | write | designed | ask=False agent=True |

## `editor_ui_navigation`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `asset_open_editor` | Open an asset in its native editor. | write | research | ask=False agent=True |
| `content_browser_navigate_folder` | Navigate Content Browser to a folder. | write | research | ask=False agent=True |
| `content_browser_sync_asset` | Focus Content Browser on an asset or folder. | write | research | ask=False agent=True |
| `editor_set_mode` | Switch editor mode (Place, Landscape, ... allow-listed). | write | research | ask=False agent=True |
| `global_tab_focus` | Bring a registered dock tab to front. | write | research | ask=False agent=True |
| `menu_command_invoke` | Execute a pre-registered editor command ID. | exec | research | ask=False agent=True |

## `landscape_foliage_pcg`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `foliage_paint_instances` | Add foliage instances in radius. | write | future | ask=False agent=True |
| `landscape_import_heightmap` | Import heightmap to landscape. | write | future | ask=False agent=True |
| `pcg_generate` | Execute PCG graph in editor. | write | future | ask=False agent=True |

## `materials_rendering`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `material_get_usage_summary` | List referencers for a material. | read | research | ask=True agent=True |
| `material_instance_set_scalar_parameter` | Set scalar on material instance. | write | research | ask=False agent=True |
| `material_instance_set_vector_parameter` | Set vector parameter on material instance. | write | research | ask=False agent=True |

## `exec`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `agent_emit_todo_plan` | Deprecated: not exposed to the model. Legacy persist path for `unreal_ai.todo_plan`. | write | deprecated | ask=False agent=False |

## `physics_collision`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `collision_trace_editor_world` | Line/sphere trace in editor world. | read | future | ask=True agent=True |
| `physics_impulse_actor` | Apply impulse (PIE-oriented). | write | future | ask=False agent=True |

## `pie_play`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `pie_start` | Start Play-In-Editor. | exec | research | ask=False agent=True |
| `pie_status` | Return whether PIE is active. | read | research | ask=True agent=True |
| `pie_stop` | Stop current PIE session. | exec | research | ask=False agent=True |

## `project_files_search`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `project_file_read_text` | Read text file under project (allow-listed paths). | read | research | ask=True agent=True |
| `project_file_write_text` | Write text file under project. | destructive | future | ask=False agent=True |
| `source_search_symbol` | Fuzzy grep project text files under Source/, Config/, and Plugins/*/Source/ (Cursor-style): typo-tolerant; ranks paths and filenames even when the user misspells or only partially remembers a name. Returns path_matches plus optional line_matches inside files. For Content assets under /Game, use asset_index_fuzzy_search (Asset Registry index). | read | implemented | ask=True agent=True |

## `selection_framing`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `editor_get_selection` | List currently selected actors. | read | research | ask=True agent=True |
| `editor_set_selection` | Replace editor selection with specified actors. | write | research | ask=False agent=True |
| `viewport_frame_actors` | Frame actors by path using bounds and FOV fit. | write | research | ask=False agent=True |
| `viewport_frame_selection` | Frame current selection in the viewport. | write | research | ask=False agent=True |

## `viewport_camera`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `viewport_camera_dolly` | Dolly or zoom the editor view. | write | research | ask=False agent=True |
| `viewport_camera_get_transform` | Read active level editor camera transform and FOV. | read | research | ask=True agent=True |
| `viewport_camera_orbit` | Orbit the active level editor camera around a pivot. | write | research | ask=False agent=True |
| `viewport_camera_pan` | Pan the active editor camera. | write | research | ask=False agent=True |
| `viewport_camera_pilot` | Pilot a CameraActor or exit pilot mode. | write | research | ask=False agent=True |
| `viewport_camera_set_transform` | Set editor camera location and rotation. | write | research | ask=False agent=True |
| `viewport_set_view_mode` | Switch editor viewport rendering mode (Lit, Wireframe, ...). | write | research | ask=False agent=True |

## `world_actors`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `actor_attach_to` | Attach child actor to parent. | write | research | ask=False agent=True |
| `actor_destroy` | Destroy an actor in the editor world. | destructive | research | ask=False agent=True |
| `actor_find_by_label` | Find actors by label or tag. | read | research | ask=True agent=True |
| `actor_get_transform` | Read actor transform and mobility. | read | research | ask=True agent=True |
| `actor_set_transform` | Set actor transform. | write | research | ask=False agent=True |
| `actor_set_visibility` | Toggle actor visibility. | write | research | ask=False agent=True |
| `actor_spawn_from_class` | Spawn an actor in the editor world. | write | research | ask=False agent=True |
| `outliner_folder_move` | Move actors to an outliner folder. | write | research | ask=False agent=True |
| `scene_fuzzy_search` | Fuzzy-search actors in the current editor level (like Cursor quick-search): matches actor labels, actor names, class names, object paths, and actor tags even when the query is approximate or slightly wrong. Use to find walls, lights, meshes, etc. without exact names. | read | implemented | ask=True agent=True |

