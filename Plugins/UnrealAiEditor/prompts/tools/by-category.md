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
| `asset_delete` | Delete assets. | destructive | implemented | ask=False agent=True |
| `asset_duplicate` | Duplicate asset to new path. | write | implemented | ask=False agent=True |
| `asset_get_metadata` | Read asset metadata and dependencies summary. | read | implemented | ask=True agent=True |
| `asset_import` | Import files via automated import pipeline. | write | future | ask=False agent=True |
| `asset_index_fuzzy_search` | Fuzzy-search the Unreal Asset Registry index (engine-maintained indexed view of content packages). Uses the same asset data the Content Browser uses; supply path_prefix (e.g. /Game or /Game/Textures) to scope. Ranks asset names, object paths, and class paths even when spelling is approximate. Prefer this over source_search_symbol for textures, materials, meshes, and other UAssets. | read | implemented | ask=True agent=True |
| `asset_registry_query` | Query Asset Registry for assets. | read | implemented | ask=True agent=True |
| `asset_rename` | Rename/move asset and optionally fix references. | destructive | implemented | ask=False agent=True |
| `asset_save_packages` | Save dirty packages. | write | implemented | ask=False agent=True |

## `audio_metasounds`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `audio_component_preview` | Preview sound in editor. | write | future | ask=False agent=True |
| `metasound_open_editor` | Open MetaSound source. | write | future | ask=False agent=True |

## `banned`

_No catalog entries._ Placeholder tools for generic network access, raw exec strings, arbitrary process/Python execution, and non–project-scoped delete were removed from `UnrealAiToolCatalog.json` (they had no native handlers). Use **`console_command`** (allow-list keys by default; optional legacy wide exec in editor settings), **`project_file_*`** / **`asset_delete`**, and other scoped tools instead.

## `blueprints`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `blueprint_add_variable` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder). Add member variable to ... | write | future | ask=False agent=True |
| `blueprint_apply_ir` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder; omitted from default mai... | write | implemented | ask=False agent=True |
| `blueprint_compile` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder; not on default main Agen... | write | implemented | ask=False agent=True |
| `blueprint_composite_lifecycle_print` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder). Composite (Tier-1): wir... | write | implemented | ask=False agent=True |
| `blueprint_export_graph_t3d` | Read-only: export graph nodes as Unreal T3D clipboard text. Exact tool_id is blueprint_expor... | read | implemented | ask=False agent=True |
| `blueprint_export_ir` | Read-only (available on main Agent and builder): export Blueprint graph IR for an existing B... | read | implemented | ask=True agent=True |
| `blueprint_format_graph` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder). Run in-process layout o... | write | implemented | ask=False agent=True |
| `blueprint_format_selection` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder). Same layout pipeline as... | write | implemented | ask=False agent=True |
| `blueprint_get_graph_summary` | Read-only (main Agent + builder): bounded summary of Blueprint graph(s) for an existing asse... | read | implemented | ask=True agent=True |
| `blueprint_graph_import_t3d` | Atomic graph mutation: resolve __UAI_G_NNNNNN__ placeholders then ImportNodesFromText on the... | write | implemented | ask=False agent=True |
| `blueprint_graph_introspect` | Read-only: enumerate nodes in a Blueprint graph with node_guid, class, title, and pins (name... | read | implemented | ask=False agent=True |
| `blueprint_graph_list_pins` | Read-only: pins[] (name, direction, category, optional default_value). Needs blueprint_path ... | read | implemented | ask=True agent=True |
| `blueprint_graph_patch` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder; not on default main Agen... | write | implemented | ask=False agent=True |
| `blueprint_open_graph_tab` | Open Blueprint editor focused on a graph. | write | implemented | ask=False agent=True |
| `blueprint_set_component_default` | Blueprint Builder sub-turn only (not default main Agent). Set one reflected property on a na... | write | implemented | ask=False agent=True |
| `blueprint_t3d_preflight_validate` | Resolve __UAI_G_NNNNNN__ placeholders in t3d_text and run CanImportNodesFromText without mut... | read | implemented | ask=False agent=True |
| `blueprint_verify_graph` | Post-edit verification: optional steps (default ["links"]): links (null/cross-graph pin link... | read | implemented | ask=False agent=True |

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
| `viewport_capture_delayed` | Schedule viewport capture after N frames. | write | implemented | ask=False agent=True |
| `viewport_capture_png` | Capture active editor viewport to PNG on disk. | write | implemented | ask=False agent=True |

## `console_exec`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `console_command` | Allow-list keys → `GEngine->Exec` (stat_fps, r_vsync+args, viewmode_*; legacy wide exec opt-in). See catalog. | exec | implemented | ask=False agent=True |

## `diagnostics_logs`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `editor_state_snapshot_read` | Deterministic editor/world snapshot JSON. | read | implemented | ask=True agent=True |
| `engine_message_log_read` | Read recent Message Log / log tail. | read | implemented | ask=True agent=True |
| `tool_audit_append` | Append line to tool audit log file. | write | designed | ask=False agent=True |

## `editor_ui_navigation`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `asset_open_editor` | Open an asset in its native editor. | write | implemented | ask=False agent=True |
| `content_browser_navigate_folder` | Navigate Content Browser to a folder. | write | implemented | ask=False agent=True |
| `content_browser_sync_asset` | Focus Content Browser on an asset or folder. | write | implemented | ask=False agent=True |
| `editor_set_mode` | Switch editor mode (Place, Landscape, ... allow-listed). | write | implemented | ask=False agent=True |
| `global_tab_focus` | Bring a registered dock tab to front. | write | implemented | ask=False agent=True |
| `menu_command_invoke` | Execute a pre-registered editor command ID. | exec | implemented | ask=False agent=True |

## `landscape_foliage_pcg`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `foliage_paint_instances` | Add foliage instances in radius. | write | future | ask=False agent=True |
| `landscape_import_heightmap` | Import heightmap to landscape. | write | future | ask=False agent=True |
| `pcg_generate` | Execute PCG graph in editor. | write | future | ask=False agent=True |

## `materials_rendering`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `material_get_usage_summary` | List referencers for a material. | read | implemented | ask=True agent=True |
| `material_instance_set_parameter` | Set scalar or vector on MI; main Agent + builder (implicit all surfaces). value_kind required. | write | implemented | ask=False agent=True |
| `material_instance_set_scalar_parameter` | Legacy branch; prefer material_instance_set_parameter. | write | deprecated | ask=False agent=False |
| `material_instance_set_vector_parameter` | Legacy branch; prefer material_instance_set_parameter. | write | deprecated | ask=False agent=False |

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
| `pie_start` | Start Play-In-Editor. | exec | implemented | ask=False agent=True |
| `pie_status` | Return whether PIE is active. | read | implemented | ask=True agent=True |
| `pie_stop` | Stop current PIE session. | exec | implemented | ask=False agent=True |

## `project_files_search`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `project_file_read_text` | Read text file under project (allow-listed paths). | read | implemented | ask=True agent=True |
| `project_file_write_text` | Write text file under project. | destructive | future | ask=False agent=True |
| `source_search_symbol` | Fuzzy grep project text files under Source/, Config/, and Plugins/*/Source/ (Cursor-style): typo-tolerant; ranks paths and filenames even when the user misspells or only partially remembers a name. Returns path_matches plus optional line_matches inside files. For Content assets under /Game, use asset_index_fuzzy_search (Asset Registry index). | read | implemented | ask=True agent=True |

## `selection_framing`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `editor_get_selection` | List currently selected actors. | read | implemented | ask=True agent=True |
| `editor_set_selection` | Replace editor selection with specified actors. | write | implemented | ask=False agent=True |
| `viewport_frame_actors` | Frame actors by path using bounds and FOV fit. | write | implemented | ask=False agent=True |
| `viewport_frame_selection` | Frame current selection in the viewport. | write | implemented | ask=False agent=True |

## `viewport_camera`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `viewport_camera_dolly` | Dolly or zoom the editor view. | write | implemented | ask=False agent=True |
| `viewport_camera_get_transform` | Read active level editor camera transform and FOV. | read | implemented | ask=True agent=True |
| `viewport_camera_orbit` | Orbit the active level editor camera around a pivot. | write | implemented | ask=False agent=True |
| `viewport_camera_pan` | Pan the active editor camera. | write | implemented | ask=False agent=True |
| `viewport_camera_pilot` | Pilot a CameraActor or exit pilot mode. | write | implemented | ask=False agent=True |
| `viewport_camera_set_transform` | Set editor camera location and rotation. | write | implemented | ask=False agent=True |
| `viewport_set_view_mode` | Switch editor viewport rendering mode (Lit, Wireframe, ...). | write | implemented | ask=False agent=True |

## `world_actors`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `actor_attach_to` | Attach child actor to parent. | write | implemented | ask=False agent=True |
| `actor_destroy` | Destroy an actor in the editor world. | destructive | implemented | ask=False agent=True |
| `actor_find_by_label` | Find actors by label or tag. | read | implemented | ask=True agent=True |
| `actor_get_transform` | Read actor transform and mobility. | read | implemented | ask=True agent=True |
| `actor_set_transform` | Set actor transform. | write | implemented | ask=False agent=True |
| `actor_set_visibility` | Toggle actor visibility. | write | implemented | ask=False agent=True |
| `actor_spawn_from_class` | Spawn an actor in the editor world. | write | implemented | ask=False agent=True |
| `outliner_folder_move` | Move actors to an outliner folder. | write | implemented | ask=False agent=True |
| `scene_fuzzy_search` | Fuzzy-search actors in the current editor level (like Cursor quick-search): matches actor labels, actor names, class names, object paths, and actor tags even when the query is approximate or slightly wrong. Use to find walls, lights, meshes, etc. without exact names. | read | implemented | ask=True agent=True |

