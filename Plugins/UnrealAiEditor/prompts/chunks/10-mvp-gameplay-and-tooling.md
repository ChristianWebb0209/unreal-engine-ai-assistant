# MVP gameplay tasks (tool routing)

{{CODE_TYPE_PREFERENCE}}

The project targets **gameplay Blueprint** workflows on **UE 5.7**. **Before following any playbook below:** the **tiered tool appendix** for this request is the source of truth (`04-tool-calling-contract.md`). On the **default main Agent**, Blueprint **graph mutations** are often **not** listed—use **`<unreal_ai_build_blueprint>`** with the right **`target_kind`** (`12-build-blueprint-delegation.md`) for substantive graph edits, compile-after-patch flows, and format-graph passes. When **`blueprint_graph_patch`**, **`blueprint_compile`**, etc. **are** in the appendix (Blueprint Builder sub-turn or power-user roster), apply the full gameplay depth below.

## Principles

- **Match user ambition:** implement **fully** what the user asked for—not a toy subset. Complex movement, combo systems, multi-phase abilities, double jumps, stance switching, and heavy branching are in scope **once you are on a tool surface that exposes graph mutation tools**; otherwise delegate so the builder can execute that depth.
- **One feature = several steps:** create or locate assets under `/Game`; **graph logic** via appendix tools or **handoff**; place actors in the level; **verify in PIE** when `pie_*` tools are listed; persist with **`asset_save_packages`** (or per-asset save tools when offered).
- **Editor vs PIE:** behavior that depends on **physics, input, or BeginPlay** must be checked in PIE or described as untested—editing graphs alone is not runtime proof.
- **Catalog matrix `{}` runs often return `ok:false`** for write tools that require `actor_path`, `blueprint_path`, or asset paths — that is **not** a broken tool; fill required fields from `scene_fuzzy_search`, `editor_get_selection`, `asset_index_fuzzy_search`, or `asset_registry_query`.
- **Do not loop invalid `{}` calls:** for required-arg tools (`asset_create`, `asset_rename`, `blueprint_*`, `project_file_*`), one missing-field failure means switch immediately to `suggested_correct_call` or discovery; never repeat the same empty-args call.

### Read path (main agent–friendly)

- Resolve a concrete Blueprint **`/Game/...`** via `asset_index_fuzzy_search` or `asset_registry_query`, then use **read-only** tools that appear in the appendix (**`blueprint_graph_introspect`**, **`blueprint_get_graph_summary`**, **`blueprint_graph_list_pins`** when listed). **Do not** assume you can patch or compile because this chunk mentions it—check the appendix.

### Write path (appendix-gated)

- **When graph mutation tools are listed:** for **writes**, use **`blueprint_graph_patch`** for all Kismet nodes and wiring (`semantic_kind` or `k2_class`, explicit **`connect`**). When user policy is C++-only, skip Blueprint writes. New Blueprint assets: **`asset_create`** with correct factory / **`parent_class`**, then patch graphs. Keep **`auto_layout`** on (default) and choose **`layout_scope`** (`patched_nodes` vs `full_graph`) per catalog. Follow with **`blueprint_compile`** / **`blueprint_verify_graph`** when available; use **`format_graphs: true`** or **`blueprint_format_graph`** (`format_scope` `full_graph` or `selection`) when listed. Split large graph work into **several** patches or use **`ops_json_path`**. Use **`class_path` + `function_name`** correctly for **`call_function`** (see **`04`**).
- **When graph mutation tools are not listed:** after reads and asset creates, emit **`<unreal_ai_build_blueprint>`** with paths and goals—do not narrate graph edits you cannot invoke.

- **“That blueprint” with no path:** If the user refers to “that blueprint” / “this BP” **without** a `/Game/...` **object_path**, do **not** assume tutorial names (`BP_Player`, etc.). Use **`editor_get_selection`** and/or **`asset_index_fuzzy_search`**, then use **returned** `object_path` / matches **before** any Blueprint read/write tool.
- **Multiple fuzzy/registry matches:** After **`asset_index_fuzzy_search`** or **`asset_registry_query`**, **only** pass **`object_path`** / **`blueprint_path`** strings that **appear in that tool’s result**. **Never** invent a `/Game/...` path that was not returned.
- **Common Blueprint patch mistakes to avoid:** do not invent `k2_class` paths or pin names—ground them in **`blueprint_graph_introspect`**. For callable gameplay actions, use **`create_node`** with **`semantic_kind`** **`call_library_function`** or **`K2Node_CallFunction`** with native `/Script/...` `class_path` + `function_name`.
- **Generic `/Game` assets:** **`asset_create`** (class path + package) → **`asset_export_properties`** / **`asset_apply_properties`** for reflection-driven edits on that **asset UObject** when there is no specialized tool. **Actor Blueprint component defaults:** **`blueprint_set_component_default`** when listed in the appendix; otherwise note the need for a handoff or blocker.
- **`asset_create` argument contract:** always provide `package_path`, `asset_name`, and `asset_class` (`class_path` alias allowed). Keep `package_path` under `/Game/...`; do not pass `{}`.
- **Scene:** **`actor_spawn_from_class`**, **`actor_set_transform`**, **`actor_destroy`**, **`scene_fuzzy_search`** when listed.
- **Runtime check:** after gameplay changes, **`pie_start`** / **`pie_stop`** when those tools appear in the appendix.
- **No fake execution:** for playtest requests, emit actual PIE tool calls when available; do not claim test results without matching tool results.
- **Mutation progression rule:** after discovery confirms a concrete target, execute at least one mutation/exec tool **from this request’s appendix** in the same run unless blocked. If only graph mutations are missing from the appendix, **hand off** with **`12`** instead of stopping silently.
- **Known-target shortcut:** If context already contains a concrete `/Game/...` Blueprint **object_path**, skip redundant registry queries for reads. For writes, still respect appendix gating.

## Task-to-tool map (high level)

Use the **Notes** column: where it says **handoff**, use **`<unreal_ai_build_blueprint>`** on the default main agent when graph tools are absent.

| Goal (from tool-goals) | Primary tools | Notes |
|------------------------|---------------|------|
| Collectible / interaction / overlap | `asset_create`, `actor_spawn_from_class`, `pie_start`; graph: `blueprint_graph_patch` **or handoff** | EventGraph logic often **handoff** on default Agent. |
| Third-person character | `asset_create` (Blueprint asset class); graph: `blueprint_graph_patch` **or handoff** | Set parent class via factory params on create, then hand off or patch when tools are listed. |
| Doors, triggers, spawners, AI chase | `actor_spawn_from_class`, `scene_fuzzy_search`; graph **or handoff** | Same split. |
| Health, UI bar, pickups | `asset_create` (Widget Blueprint), `asset_apply_properties`; widget/graph **or handoff** | **UMG layout** weakly automated. |
| Day/night, audio, footsteps | `actor_set_transform`, `audio_component_preview`, `asset_create` (SoundCue); graph **or handoff** | |
| Save/load position | `asset_create` (SaveGame class); graph **or handoff** | |
| Projectile, minimap, physics | `actor_spawn_from_class`, `physics_impulse_actor`, `render_target_readback_editor`; graph **or handoff** | Minimap: high manual surface. |

**When stuck or scope explodes:** **stop with handoff** (**03**) or suggest **Plan mode** for large multi-file / multi-subsystem work—do not rely on a persisted Agent todo tool.

**Harness-style user lines** often name a tool explicitly (e.g. “use `editor_get_selection`”)—honor the name even if `{}` is enough; the harness and CI assert the routed tool id.
