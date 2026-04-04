# Tool catalog by category (prompt-facing)

Derived from `UnrealAiToolCatalog.json`. Table cells list **ask** and **agent**; **`plan`** appears only in the JSON (`modes.plan` per tool).

## `animation_sequencer`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `animation_blueprint_get_graph_summary` | Summarize AnimBlueprint graphs. | read | future | ask=True agent=True |
| `level_sequence_create_asset` | Create a Level Sequence asset under /Game (typed shortcut over asset_create). | write | implemented | ask=False agent=True |
| `sequencer_open` | Open Level Sequence in Sequencer. | write | future | ask=False agent=True |

## `assets_content`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `asset_apply_properties` | Write/apply flat JSON property deltas on the loaded asset UObject under /Game (reflecti... | write | implemented | ask=False agent=True |
| `asset_create` | Create a new asset under /Game via IAssetTools (asset_class path + optional factory_cla... | write | implemented | ask=False agent=True |
| `asset_delete` | Delete assets. | destructive | implemented | ask=False agent=True |
| `asset_duplicate` | Duplicate asset to new path. | write | implemented | ask=False agent=True |
| `asset_export_properties` | Read/export editable UObject properties under /Game as JSON (reflection). For a Bluepri... | read | implemented | ask=True agent=True |
| `asset_find_referencers` | Compatibility-only asset graph branch. Prefer asset_graph_query with relation=referencers. | read | deprecated | ask=False agent=False |
| `asset_get_dependencies` | Compatibility-only asset graph branch. Prefer asset_graph_query with relation=dependenc... | read | deprecated | ask=False agent=False |
| `asset_get_metadata` | Read asset metadata and dependencies summary. | read | implemented | ask=True agent=True |
| `asset_graph_query` | Read Asset Registry graph relations for one asset. Required: relation (referencers\|depe... | read | implemented | ask=True agent=True |
| `asset_import` | Import files via automated import pipeline. | write | future | ask=False agent=True |
| `asset_index_fuzzy_search` | Fuzzy-search the Asset Registry under path_prefix (default /Game). On vague but domain-... | read | implemented | ask=True agent=True |
| `asset_registry_query` | Query Asset Registry for deterministic listing by exact path/class filters. You must pa... | read | implemented | ask=True agent=True |
| `asset_rename` | Rename/move asset and optionally fix references. | destructive | implemented | ask=False agent=True |
| `asset_save_packages` | Persist dirty package changes to disk. Use after compile/edit/mutation passes so editor... | write | implemented | ask=False agent=True |

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
| `blueprint_compile` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder; not on default main... | write | implemented | ask=False agent=True |
| `blueprint_format_graph` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder). Run in-process lay... | write | implemented | ask=False agent=True |
| `blueprint_get_graph_summary` | Read-only (main Agent + builder): bounded summary of Blueprint graph(s) for an existing... | read | implemented | ask=True agent=True |
| `blueprint_graph_introspect` | Read-only: canonical graph discovery for patch workflows. Enumerates nodes with node_gu... | read | implemented | ask=False agent=True |
| `blueprint_graph_list_pins` | Read-only: pins[] (name, direction, category, optional default_value). Needs blueprint_... | read | implemented | ask=True agent=True |
| `blueprint_graph_patch` | Blueprint Builder sub-turn only (agent_surfaces: blueprint_builder; not on default main... | write | implemented | ask=False agent=True |
| `blueprint_set_component_default` | Blueprint Builder sub-turn only (not default main Agent). Set one reflected property on... | write | implemented | ask=False agent=True |
| `blueprint_verify_graph` | Post-edit verification: optional steps (default ["links"]): links, orphan_pins, duplica... | read | implemented | ask=False agent=True |

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
| `viewport_capture` | Capture the editor viewport. Required: capture_kind immediate_png or after_frames (expl... | write | implemented | ask=False agent=True |
| `viewport_capture_delayed` | Compatibility-only viewport capture branch. Prefer viewport_capture with capture_kind=a... | write | deprecated | ask=False agent=False |
| `viewport_capture_png` | Compatibility-only viewport capture branch. Prefer viewport_capture with capture_kind=i... | write | deprecated | ask=False agent=False |

## `console_exec`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `console_command` | Run a bounded editor console exec via allow-list keys (default). command must be a key:... | exec | implemented | ask=False agent=True |

## `diagnostics_logs`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `editor_state_snapshot_read` | Read a deterministic editor/world snapshot (JSON). Use when context may be stale or bef... | read | implemented | ask=True agent=True |
| `engine_message_log_read` | Read recent Message Log / log tail. | read | implemented | ask=True agent=True |
| `tool_audit_append` | Append line to tool audit log file. | write | designed | ask=False agent=True |

## `editor_ui_navigation`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `asset_open_editor` | Open an asset in its native editor when you already have an exact object path (for exam... | write | implemented | ask=False agent=True |
| `content_browser_navigate_folder` | Navigate Content Browser to a folder. | write | implemented | ask=False agent=True |
| `content_browser_sync_asset` | Focus the Content Browser on a registered asset by object path (`path` / `object_path`)... | write | implemented | ask=False agent=True |
| `editor_get_mode` | Read the current active editor mode. | read | implemented | ask=True agent=True |
| `editor_set_mode` | Switch editor mode (Place, Landscape, ... allow-listed). | write | implemented | ask=False agent=True |
| `global_tab_focus` | Bring a registered dock tab to front. | write | implemented | ask=False agent=True |
| `menu_command_invoke` | Execute a pre-registered editor command ID. | exec | implemented | ask=False agent=True |

## `exec`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `agent_emit_todo_plan` | Deprecated: not exposed to the model. Persist unreal_ai.todo_plan via tool call (legacy... | write | deprecated | ask=False agent=False |

## `landscape_foliage_pcg`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `foliage_paint_instances` | Add foliage instances in radius. | write | future | ask=False agent=True |
| `landscape_import_heightmap` | Import heightmap to landscape. | write | future | ask=False agent=True |
| `pcg_generate` | Execute PCG graph in editor. | write | future | ask=False agent=True |

## `materials_rendering`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `material_get_usage_summary` | List referencers for a material. If not found in the registry, the path may be wrong fo... | read | implemented | ask=True agent=True |
| `material_graph_compile` | Blueprint Builder material_graph: recompile a base Material after graph edits (UMateria... | write | implemented | ask=False agent=True |
| `material_graph_export` | Blueprint Builder material_graph: structured export of base Material expressions (GUID,... | read | implemented | ask=True agent=True |
| `material_graph_patch` | Blueprint Builder material_graph only: batched ops on a base Material — add_expression ... | write | implemented | ask=False agent=True |
| `material_graph_summarize` | Read-only inventory of a base Material's expression graph (class + editor position + st... | read | implemented | ask=True agent=True |
| `material_graph_validate` | Blueprint Builder material_graph: list orphan expressions (not reachable from any mater... | read | implemented | ask=True agent=True |
| `material_instance_get_scalar_parameter` | Read one scalar parameter value from a Material Instance. | read | implemented | ask=True agent=True |
| `material_instance_get_vector_parameter` | Read one vector parameter value from a Material Instance. | read | implemented | ask=True agent=True |
| `material_instance_set_parameter` | Set a scalar or vector parameter on a Material Instance (main Agent and builder; not a ... | write | implemented | ask=False agent=True |
| `material_instance_set_scalar_parameter` | Compatibility-only scalar branch for Material Instance parameter writes. Prefer materia... | write | deprecated | ask=False agent=False |
| `material_instance_set_vector_parameter` | Compatibility-only vector branch for Material Instance parameter writes. Prefer materia... | write | deprecated | ask=False agent=False |

## `physics_collision`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `collision_trace_editor_world` | Line/sphere trace in editor world (collision, raycast, hit from camera or points). | read | future | ask=True agent=True |
| `physics_impulse_actor` | Apply impulse (PIE-oriented). | write | future | ask=False agent=True |

## `pie_play`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `pie_start` | Start Play-In-Editor. Optional mode: "viewport" (default, in-process PIE) or "standalon... | exec | implemented | ask=False agent=True |
| `pie_status` | Return PIE / play-session state: playing_in_editor, play_session_request_queued, play_s... | read | implemented | ask=True agent=True |
| `pie_stop` | Stop current PIE session started via pie_start. Use when playtest/run requests are comp... | exec | implemented | ask=False agent=True |

## `project_files_search`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `cpp_project_compile` | Compile via Engine Build.bat (Windows). Blocked in normal interactive editor unless con... | exec | implemented | ask=False agent=True |
| `project_file_move` | Move or rename under the project. Source and destination must both be under Saved/Unrea... | destructive | implemented | ask=False agent=True |
| `project_file_read_text` | Read a text file anywhere under the project directory (path relative to project root; S... | read | implemented | ask=True agent=True |
| `project_file_write_text` | Write a text file under the project. Default safe location: Saved/UnrealAiEditorAgent/ ... | destructive | future | ask=False agent=True |
| `source_search_symbol` | Fuzzy grep project text files under Source/, Config/, and Plugins/*/Source/ (Cursor-sty... | read | implemented | ask=True agent=True |

## `properties_settings`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `entity_get_property` | Read a single allowlisted property from an entity (currently actor-focused). Prefer thi... | read | implemented | ask=True agent=True |
| `entity_set_property` | Set a single allowlisted property on an entity (currently actor-focused). Prefer this f... | write | implemented | ask=False agent=True |

## `selection_framing`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `editor_get_selection` | Return only the current editor selection (paths and labels); may be empty. Do not use a... | read | implemented | ask=True agent=True |
| `editor_set_selection` | Replace editor selection with level actors only. Paths must be world/level actor paths ... | write | implemented | ask=False agent=True |
| `viewport_frame` | Frame the viewport on selection or explicit actors. Required: target selection or actor... | write | implemented | ask=False agent=True |
| `viewport_frame_actors` | Compatibility-only viewport frame branch. Prefer viewport_frame with target=actors. | write | deprecated | ask=False agent=False |
| `viewport_frame_selection` | Compatibility-only viewport frame branch. Prefer viewport_frame with target=selection. | write | deprecated | ask=False agent=False |

## `settings_properties`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `setting_apply` | Write one allowlisted setting using the domain/key settings envelope. Prefer this over ... | write | implemented | ask=False agent=True |
| `setting_query` | Read one allowlisted setting using the domain/key settings envelope. Prefer this over s... | read | implemented | ask=True agent=True |
| `settings_get` | Compatibility-only legacy settings reader. Prefer setting_query for model-facing retrie... | read | deprecated | ask=False agent=False |
| `settings_set` | Compatibility-only legacy settings writer. Prefer setting_apply for model-facing retrie... | write | deprecated | ask=False agent=False |

## `viewport_camera`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `viewport_camera_control` | Unified viewport camera tool. Required: operation (dolly\|orbit\|pan\|pilot\|get_transform\|... | write | implemented | ask=True agent=True |
| `viewport_camera_dolly` | Compatibility-only viewport camera branch. Prefer viewport_camera_control with operatio... | write | deprecated | ask=False agent=False |
| `viewport_camera_get_transform` | Compatibility-only viewport camera branch. Prefer viewport_camera_control with operatio... | read | deprecated | ask=False agent=False |
| `viewport_camera_orbit` | Compatibility-only viewport camera branch. Prefer viewport_camera_control with operatio... | write | deprecated | ask=False agent=False |
| `viewport_camera_pan` | Compatibility-only viewport camera branch. Prefer viewport_camera_control with operatio... | write | deprecated | ask=False agent=False |
| `viewport_camera_pilot` | Compatibility-only viewport camera branch. Prefer viewport_camera_control with operatio... | write | deprecated | ask=False agent=False |
| `viewport_camera_set_transform` | Compatibility-only viewport camera branch. Prefer viewport_camera_control with operatio... | write | deprecated | ask=False agent=False |
| `viewport_get_view_mode` | Read the current editor viewport rendering mode. | read | implemented | ask=True agent=True |
| `viewport_set_view_mode` | Switch editor viewport rendering mode (Lit, Wireframe, Unlit, …). Does not require acto... | write | implemented | ask=False agent=True |

## `world_actors`

| tool_id | Summary | permission | status | modes |
|---------|---------|------------|--------|-------|
| `actor_attach_to` | Attach child actor to parent. | write | implemented | ask=False agent=True |
| `actor_blueprint_toggle_visibility` | Toggle whether the actor is temporarily hidden in the editor (flips current state). Pre... | write | implemented | ask=False agent=True |
| `actor_destroy` | Destroy an actor in the editor world. Refused for engine streaming/HLOD/WorldPartition ... | destructive | implemented | ask=False agent=True |
| `actor_find_by_label` | Find actors by outliner label, internal actor name, full object path substring, or exac... | read | implemented | ask=True agent=True |
| `actor_get_transform` | Read actor transform and mobility. | read | implemented | ask=True agent=True |
| `actor_get_visibility` | Read whether an actor is currently hidden in the editor. | read | implemented | ask=True agent=True |
| `actor_set_transform` | Mutate/set actor transform in the loaded level. Use this after scene_fuzzy_search when ... | write | implemented | ask=False agent=True |
| `actor_set_visibility` | Toggle actor visibility. | write | implemented | ask=False agent=True |
| `actor_spawn_from_class` | Spawn an actor in the editor world. Requires class_path + location[3] + rotation[3]. If... | write | implemented | ask=False agent=True |
| `outliner_folder_move` | Move actors to an outliner folder. | write | implemented | ask=False agent=True |
| `scene_fuzzy_search` | Fuzzy-search actors across the loaded editor world/level (not Content Browser assets). ... | read | implemented | ask=True agent=True |
