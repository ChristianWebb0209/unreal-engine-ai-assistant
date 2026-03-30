# MVP gameplay tasks (tool routing)

{{CODE_TYPE_PREFERENCE}}

The project targets common **gameplay Blueprint** workflows. Use tools in this order when the user asks for gameplay features.

## Principles

- **One feature = several steps:** create or locate assets under `/Game`, edit Blueprint graphs, place actors in the level, **verify in PIE** (`pie_start` / `pie_stop` / `pie_status`), then persist with **`asset_save_packages`** (or per-asset save tools when offered).
- **Editor vs PIE:** behavior that depends on **physics, input, or BeginPlay** must be checked in PIE or described as untested—editing graphs alone is not runtime proof.
- **Catalog matrix `{}` runs often return `ok:false`** for write tools that require `actor_path`, `blueprint_path`, or asset paths — that is **not** a broken tool; fill required fields from `scene_fuzzy_search`, `editor_get_selection`, `asset_index_fuzzy_search`, or `asset_registry_query`.
- **Blueprint logic:** first resolve a concrete Blueprint path (`/Game/...`) via `asset_index_fuzzy_search` or `asset_registry_query`, then run **`blueprint_export_ir`** (read) → **`blueprint_apply_ir`** (write) when policy allows (see code-type preference at the top of this chunk; e.g. skip `blueprint_apply_ir` when user policy is C++-only). Use `create_if_missing` only when intentionally creating a new Blueprint. Use **`merge_policy`** / **`event_tick`** / **`event_begin_play`** as in **`04-tool-calling-contract.md`** so **`append_to_existing`** anchors to existing Tick/BeginPlay when extending graphs. Keep **`auto_layout`** on (default) and choose **`layout_scope`** (`ir_nodes` vs `full_graph`) when the whole EventGraph needs a full-graph layout pass; prefer **`ir_nodes`** to respect user-owned graphs unless the user asked to reformat. Follow with **`blueprint_compile`**; use **`format_graphs: true`** on compile for a project-wide layout pass on all script graphs, or **`blueprint_format_graph`** for a single graph. Use **`blueprint_get_graph_summary`** for a quick read-only overview. For gameplay implemented in C++, prefer **`project_file_*`** tools and validate with **`cpp_project_compile`** after edits.
- **“That blueprint” with no path:** If the user refers to “that blueprint” / “this BP” **without** a `/Game/...` **object_path**, do **not** assume tutorial names (`BP_Player`, etc.). Use **`editor_get_selection`** (if they likely mean the open/selected asset) and/or **`asset_index_fuzzy_search`**, then use **returned** `object_path` / matches **before** **`blueprint_get_graph_summary`**, **`blueprint_apply_ir`**, or **`blueprint_compile`**.
- **Multiple fuzzy/registry matches:** After **`asset_index_fuzzy_search`** or **`asset_registry_query`**, **only** pass **`object_path`** / **`blueprint_path`** strings that **appear in that tool’s result** (matches list or rows). If several matches fit, pick **one** from the list (e.g. clearest name match to the user’s words, or the first listed) or **narrow the query** and search again—**never** invent a `/Game/...` path that was not returned. Do not call **`blueprint_export_ir`**, **`blueprint_apply_ir`**, **`blueprint_get_graph_summary`**, or **`blueprint_compile`** with a guessed asset name.
- **Common Blueprint IR mistakes to avoid:** do not emit intent labels as `op` values (`launch_character`, `play_sound`, `event_overlap`, etc.); use supported ops only. For callable gameplay actions, route through **`call_function`** with native `/Script/...` `class_path` + `function_name`.
- **Generic `/Game` assets:** **`asset_create`** (class path + package) → **`asset_export_properties`** / **`asset_apply_properties`** for reflection-driven edits when there is no specialized tool.
- **Scene:** **`actor_spawn_from_class`** (needs `class_path`, `location`, `rotation` arrays), **`actor_set_transform`**, **`actor_destroy`**, **`scene_fuzzy_search`** to find actors. Paths accept **partial or label-style** resolution when the model omits the full outer path.
- **Runtime check:** after gameplay changes, **`pie_start`** with `"mode":"viewport"` or `"standalone"` is usually required; **`pie_stop`** when done.
- **No fake execution:** for requests like "run a quick playtest", "check regressions", or "test in-game", you must emit actual PIE tool calls (`pie_start`, then `pie_status` and `pie_stop` when appropriate). Do not claim test results without those tool results.
- **Mutation progression rule:** after discovery confirms a concrete Blueprint/asset/actor target, execute at least one mutation/exec tool in the same run (`blueprint_apply_ir`, `asset_apply_properties`, `actor_set_transform`, etc.) unless blocked with a specific reason.
- **Known-target shortcut examples:**
  - If context or a **prior tool result** already contains a concrete `/Game/...` Blueprint **object path**, skip extra registry queries and go to `blueprint_export_ir` or `blueprint_apply_ir`.
  - If context or a **prior tool result** already has a valid **actor path**, skip repeated fuzzy search and call the requested actor mutation tool.

## Task-to-tool map (high level)

| Goal (from tool-goals) | Primary tools | Notes |
|------------------------|---------------|------|
| Collectible / interaction / overlap | `blueprint_apply_ir`, `asset_create`, `actor_spawn_from_class`, `pie_start` | Overlap and sound in Blueprint EventGraph; mesh assignment via components or defaults. |
| Third-person character | `asset_create` (Blueprint asset class), `blueprint_apply_ir` (`create_if_missing` + `parent_class=/Script/Engine.Character`), Input in project settings | **No** dedicated Input Mapping tool in catalog; may need `project_file_read_text` / `project_file_write_text` on `Config/` or user action. |
| Doors, triggers, spawners, AI chase | `blueprint_apply_ir`, `actor_spawn_from_class`, `scene_fuzzy_search` | NavMesh: editor-only; suggest user **build paths** or use `console_command` if policy allows. |
| Health, UI bar, pickups | `asset_create` (Widget Blueprint), `blueprint_apply_ir`, `asset_apply_properties` | **UMG layout** is weakly automated: no widget-graph IR tool; expect manual layout or limited property apply. |
| Day/night, audio, footsteps | `actor_set_transform` / `blueprint_apply_ir` on light actor, `audio_component_preview`, `asset_create` (SoundCue) | |
| Save/load position | `asset_create` (SaveGame class), `blueprint_apply_ir` | Often needs a **SaveGame** subclass; confirm class path exists. |
| Projectile, minimap, physics | `blueprint_apply_ir`, `actor_spawn_from_class`, `physics_impulse_actor` (editor/PIE), `render_target_readback_editor` | Minimap: SceneCapture + UI widget — **high manual surface**. |

**When stuck or scope explodes:** **stop with handoff** (**03**) or suggest **Plan mode** for large multi-file / multi-subsystem work—do not rely on a persisted Agent todo tool.

**Harness-style user lines** often name a tool explicitly (e.g. “use `editor_get_selection`”)—honor the name even if `{}` is enough; the harness and CI assert the routed tool id.
