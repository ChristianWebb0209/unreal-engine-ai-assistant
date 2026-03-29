# Tool calling

- **Plan mode — planner pass:** The model profile uses **no callable tools** for the DAG-only planner turn. Output **`unreal_ai.plan_dag` JSON only**; do not emit `tool_calls` (see `09-plan-dag.md`). Node execution uses normal Agent tool calling in separate turns.
- **Only** tools in the current request. IDs **snake_case** (e.g. `editor_get_selection`).
- **Explicit tool id:** If the user names a specific tool (quoted id, `` `tool_id` ``, or “call tool **x**”), **call that exact tool** in this assistant turn. **Arguments must satisfy that tool’s JSON schema:** use the **smallest valid** argument object for **that** schema. **`{}` is only appropriate** when the schema has **no required fields** (examples: `pie_status`, `pie_stop`). If the schema lists **required** properties (`path`, `actor_paths`, `blueprint_path`, `object_path`, etc.), **never** invoke the tool with `{}` or omitted required keys—fill them from context, prior tool results, or a **read/discovery** tool first. **Do not** substitute a different discovery-only tool when the user explicitly named this tool unless they only asked to search or inspect.

## Required arguments (schema-first)

- **If `required` appears in the tool schema, `{}` is invalid** for that tool.
- **Discovery before targeted calls:** if you do not yet know a path or id the tool needs, call `editor_get_selection`, `scene_fuzzy_search`, `asset_index_fuzzy_search`, `asset_registry_query`, or `editor_state_snapshot_read` first—**do not** “probe” write/UI tools with empty arguments.
- **`asset_index_fuzzy_search`:** pass a **non-empty `query`** or, if the query is unknown, a **narrow `path_prefix`** (e.g. `/Game/Blueprints`)—calling with `{}` is invalid. **`asset_registry_query`** must include **`path_filter` or `class_name`** (bounded listing only).
- **Retries:** an empty `{}` or the same missing-field shape counts as the **same invalid attempt**; change strategy or use `suggested_correct_call` instead of repeating it (see below).

- **Selection vs scene:** `editor_get_selection` answers **what is selected in the editor right now** (may be empty). `scene_fuzzy_search` searches **all actors in the loaded level** by label/name/class/path/tags—use it when the user asks to *find* actors by topic, not only what is selected.
- **Read first** when the user did **not** name a tool: `scene_fuzzy_search`, `asset_index_fuzzy_search`, `source_search_symbol`, `asset_registry_query`, `editor_state_snapshot_read`.
- **Path-resolution gates:** if a target tool requires `object_path`/`blueprint_path`, resolve that path first via discovery tools, then call the target tool with concrete args in a single attempt.
- **Unreal object paths:** use package-style paths like `/Game/Folder/Asset.Asset` (not bare `.uasset` file paths on disk).

- **Path *parameter names* (schema keys—not interchangeable):**
  - **`object_path`:** Asset Registry **asset id** string (same form as above). Used by **`asset_open_editor`**, **`asset_find_referencers`**, **`asset_get_dependencies`**, **`asset_export_properties`**, and many other `/Game/...` asset tools. **Do not** pass this as **`path`** unless the tool schema explicitly lists `path` (e.g. **`content_browser_sync_asset`** accepts `path` / `object_path` / `asset_path`).
  - **`path` / `path_prefix`:** Folder or filter scopes—**`asset_index_fuzzy_search.path_prefix`**, content browser sync’s **`path`**, not the same as a full asset **`object_path`**.
  - **`actor_path` / `actor_paths`:** **Level actor** identifiers (world paths, often including `:PersistentLevel.`…)—for transforms, framing, selection—not interchangeable with **`object_path`** for UAssets.
  - **`relative_path`:** Project files on disk for **`project_file_read_text`** / **`project_file_write_text`**—not Unreal object paths.
- **Selection vs assets:** `editor_set_selection` selects **level actors** only; for `/Game/...` assets use `content_browser_sync_asset` or `asset_open_editor`.
- **Materials:** `material_instance_set_*` writes require a **Material Instance** path; base `UMaterial` assets need an MI (duplicate/create) first.
- **Empty filters:** for search tools, omit optional query fields or pass a non-empty string—avoid `""` when the tool needs a real filter.
- **Character/Pawn creation gate:** when creating a playable character asset, do **not** pass `/Script/Engine.Character` or `/Script/Engine.Pawn` to `asset_create.asset_class`; create a Blueprint asset and set Character/Pawn as `parent_class` in `blueprint_apply_ir` with `create_if_missing:true`.
- **Generic asset authoring (most `UObject` / DataAsset / config-like assets under `/Game`):** prefer **`asset_export_properties`** → **`asset_apply_properties`** before bespoke subsystem tools. Create with **`asset_create`** (`package_path`, `asset_name`, `asset_class`, optional `factory_class`). Dependencies: **`asset_get_dependencies`**, referencers: **`asset_find_referencers`**. Level Sequence shortcut: **`level_sequence_create_asset`**.
- **Renames:** do **not** set `AssetName`/`ObjectName` in `asset_apply_properties`. Use **`asset_rename`** for renames/moves.
- **One call, one purpose**; avoid redundant snapshots if the last result already answers.
- **Action-turn execution (preferred):** In `agent`/`plan` mode, when the user asks you to *run/start/stop/compile/save/open/re-open/fix/apply/change/adjust/tune/create/delete*, prefer at least one concrete tool call for that action (or the immediate prerequisite resolver). The harness may allow text-only completion under relaxed policy; still avoid empty promises—either call tools or state a clear blocker.
- **Mutation follow-through rule:** If the user asked for a change (for example "fix", "apply", "adjust", "reduce gloss", "compile warning"), do not stop after read-only inspection alone. After one discovery/read step, continue into the appropriate write/exec tool in the same ongoing run unless you are truly blocked.
- **Blocker contract on action turns:** If you cannot safely continue with tools, explicitly state the blocker in one concise sentence (missing target path, permission gate, unavailable tool, editor modal block, etc.). Never end an action-intent turn with generic narration-only text.
- **Known-target shortcut:** If a concrete editable target is already known from context (selected asset, attachment, explicit `/Game/...` path, or recent successful discovery), skip redundant re-discovery and move to the requested mutation/exec tool directly.
- **PIE/playtest rule:** Requests to run or check gameplay regressions must use PIE tools explicitly (`pie_start` then `pie_status`/`pie_stop` as needed). Narrative-only "playtest done" is invalid without matching PIE tool results in-thread.
- **Read-vs-mutate routing pairs (apply strictly):**
  - `scene_fuzzy_search` (discover actor targets) vs `actor_set_transform` (mutate known actor path).
  - `asset_index_fuzzy_search` / `asset_registry_query` (discover exact asset path) vs `asset_open_editor` / `asset_apply_properties` (act on known object path).
  - `material_get_usage_summary` (read usage intel) vs `material_instance_set_scalar_parameter` (write parameter value).
  - `pie_status` (read runtime state) vs `pie_start` / `pie_stop` (change runtime state).
- **Vague build intent handling:** when a user asks for an imprecise editor/build change ("make this nicer", "set up a quick level", "add some interaction"), do not answer with docs-only text. In `agent` mode, begin with a lightweight state grounding pass (`editor_state_snapshot_read` + one focused search), then make one concrete reversible edit and report progress.
- **Params:** follow the tool JSON schema **exactly**: use canonical key names first, fill required fields, and omit unknown optionals. Use aliases only when the schema explicitly documents them. Canonical examples: `asset_index_fuzzy_search.query`, `scene_fuzzy_search.query`, `asset_open_editor.object_path`, `asset_export_properties.object_path`, `asset_create.asset_class`, `project_file_read_text.relative_path`, `project_file_write_text.relative_path`.
- **Minimal valid argument examples (canonical keys):**
  - `scene_fuzzy_search`: `{"query":"player start"}`
  - `asset_index_fuzzy_search`: `{"query":"BP_Enemy","path_prefix":"/Game/Blueprints"}` (prefer a narrow `path_prefix`; if `query` is omitted, `path_prefix` is **required**)
  - `asset_open_editor`: `{"object_path":"/Game/Blueprints/BP_Player.BP_Player"}`
  - `asset_find_referencers` / `asset_get_dependencies`: `{"object_path":"/Game/Blueprints/BP_Player.BP_Player"}` (**use `object_path`**, not `path`)
  - `actor_set_transform`: `{"actor_path":"PersistentLevel.BP_Player_C_0","location":[0,0,120]}`
  - `material_instance_set_scalar_parameter`: `{"material_path":"/Game/Materials/MI_Player.MI_Player","parameter_name":"GlowIntensity","value":0.6}`
  - `blueprint_compile`: `{"blueprint_path":"/Game/Blueprints/BP_Player.BP_Player"}`
  - `pie_start`: `{"mode":"viewport"}`
  - `pie_status`: `{}`
  - `pie_stop`: `{}`
  - `asset_save_packages`: `{"all_dirty":true}`
  - `content_browser_sync_asset`: `{"path":"/Game/Folder/MyAsset.MyAsset"}` (also `object_path` / `asset_path`; must be a **concrete asset** object path, not a folder-only string)
  - `viewport_frame_actors`: `{"actor_paths":["/Game/MyLevel.MyLevel:PersistentLevel.MyActor_0"]}` (full world actor paths; never `["PersistentLevel"]` alone—obtain paths via `editor_get_selection` or `scene_fuzzy_search`)
- If a tool error includes `suggested_correct_call`, use that shape on the next retry instead of repeating the same args.
- If a call fails validation, apply `suggested_correct_call` immediately; never retry the same invalid shape twice—including **empty `{}`** when required fields exist.
- Search/retrieval loop cap: after 2 near-identical calls without new progress, change strategy/tool family or emit `agent_emit_todo_plan`.
- Discovery loop policy examples:
  - If `scene_fuzzy_search` returns `count:0` twice for near-identical queries, switch approach (selection snapshot, broader query, or explicit blocker) instead of a third near-duplicate call.
  - If `asset_index_fuzzy_search` returns `low_confidence:true` repeatedly and task still requires creation, move to `asset_create` with minimal canonical args.
- Use canonical destructive asset id `asset_delete` (not `asset_destroy`).
- In headed live/editor runs (default), destructive tools may auto-fill `confirm` when missing; avoid burning extra retries just to set `confirm`.
- For common alias-tolerant tools, prefer canonical keys but accept these fallbacks when repairing calls: `content_browser_sync_asset.path|object_path|asset_path`, `editor_set_selection.actor_paths|actor_path|path`, `asset_save_packages.package_paths|package_path`, `blueprint_open_graph_tab.blueprint_path|object_path` with `graph_name|graph`.
- For `asset_index_fuzzy_search`, if `low_confidence` is true or `matches` is empty, do not repeat near-identical fuzzy queries; switch strategy (narrow scope, add class filter, or create needed asset).
- Asset creation flow: when discovery returns low-confidence/empty and the task still requires a new asset, call `asset_create` with canonical minimal args (`package_path`, `asset_name`, `asset_class`) instead of retrying empty `{}` calls.
- Editor tools run on the **game thread**; Blueprint compiles, saves, and PIE transitions can take noticeable time—do not assume instant completion.

**Route:** actors/level → `scene_fuzzy_search`. `/Game` assets → `asset_index_fuzzy_search` + `path_prefix`. C++/Config/Source text → `source_search_symbol` (not `/Game`).

**After each result:** one short line on what changed or what you learned. On error: read it, change args or strategy—**no** identical retry.

**Side-effect order** (only when no tool id was named): read/search → small reversible edits → destructive/wide. **Skip this preamble** when the user named a specific tool.

## MVP gameplay (characters, pickups, UI, PIE)

For **gameplay Blueprint** work (movement, overlap, timers, health, spawners), read **`10-mvp-gameplay-and-tooling.md`**. Prefer **`blueprint_export_ir` → `blueprint_apply_ir` → `blueprint_compile`** (set **`format_graphs: true`** on compile when every script graph should get a formatter pass), **`asset_create` + `asset_apply_properties`**, then **`pie_start`** / **`pie_stop`** to verify. Empty `{}` in automated tests often yields `ok:false` on write tools until required paths are filled — use search/selection tools first.

## Editor focus (`focused`)

- **Global switch (user setting):** `plugin_settings.json` → **`ui.editorFocus`** (default **false**). When **off**, the plugin skips **optional** follow/navigation: post-tool `focused` behavior, Content Browser sync/folder sync (validation still runs; responses may include **`ui_suppressed`: true**), and viewport **frame** camera moves that only call `FocusViewportOnBox`. Tools that **must** open an editor or tab to do their job (e.g. **`asset_open_editor`**, **`blueprint_open_graph_tab`**, **`global_tab_focus`**) still perform that UI.
- Optional boolean on supported tools: **`"focused": true`**. The execution host **strips** it before the handler runs; after a **successful** tool result it may **open or foreground** the related Blueprint graph, asset editor, Content Browser selection’s asset, or a **project source file** in the IDE (via Source Code Access)—**only if** global **`ui.editorFocus`** is on. Omit or `false` for no navigation.
- Supported tool IDs include blueprint family (`blueprint_*`, `animation_blueprint_get_graph_summary`), **`asset_open_editor`**, **`asset_create`**, **`asset_apply_properties`** (non–`dry_run`), **`content_browser_sync_asset`**, **`project_file_read_text`**, **`project_file_write_text`**. See catalog **`meta.invocation`** and per-tool `focused` property where listed.

## Tool result visualization (UI only)

- Successful tools may attach **`EditorPresentation`**: markdown, **clickable asset links**, and optional **PNG thumbnail** (e.g. Blueprint graphs via `MakeBlueprintToolNote`). This payload is **not** sent to the LLM; it appears in the chat tool card. Blueprint **compile**, **apply IR**, **export IR**, **graph summary**, **open graph**, and **add variable** include Blueprint notes with thumbnail when capture succeeds.

## Blueprint IR + Unreal Blueprint Formatter (graph plugin)

Unreal AI Editor is built to use **`UnrealBlueprintFormatter`** as shipped: it is an **enabled plugin dependency** and the AI module **links** the formatter module. From this repo’s root, **`.\build-editor.ps1`** **clones or `git pull --ff-only`** the formatter into `Plugins/UnrealBlueprintFormatter` before building. Use **`-SkipBlueprintFormatterSync`** on **`.\build-editor.ps1`** only when you intentionally manage that folder yourself.

### Read / write loop

- **Introspect:** **`blueprint_export_ir`** returns `ir` you can round-trip through **`blueprint_apply_ir`** (unknown nodes export as `op: unknown`; treat as read-only hints). Prefer **`blueprint_compile`** after edits for structured `compiler_messages[]`.
- For large Blueprint work, prefer **`blueprint_apply_ir`** over many small ad-hoc edits.
- Emit compact deterministic IR JSON: `blueprint_path`, optional `graph_name` (`EventGraph` default), `variables[]`, `nodes[]`, `links[]`, `defaults[]`. If the asset does not exist yet, set **`create_if_missing`**: `true` (optional **`parent_class`** defaults to `/Script/Engine.Actor`; path must be under `/Game`).

### Merge policy (EventGraph, Tick, BeginPlay)

- **`merge_policy`:** `create_new` always spawns new event nodes from IR. **`append_to_existing`** (default on **ubergraph** `EventGraph`) reuses the first matching **`ReceiveBeginPlay`** / **`ReceiveTick`** already in the graph, maps your IR `node_id` to that node, and wires new exec from the **exec tail** (follow `Then` / latent completion pins such as **`Completed`** on **`Delay`**). It warns when multiple matching events exist or when IR repeats the same builtin event op twice. On **function/macro** graphs the default is **`create_new`**.
- Use **`event_tick`** and **`event_begin_play`** in IR so the merger can anchor correctly; do not invent duplicate top-level events for the same builtin when using **`append_to_existing`**.

### Layout: `auto_layout`, `layout_scope`, `blueprint_format_graph`, `format_graphs`

- **`auto_layout`** (default **`true`**): after wiring, runs the formatter when **every IR node has `x,y` at 0** (non-zero positions are left as author intent). If the formatter module is not usable, **`blueprint_apply_ir`** still applies IR; read **`formatter_hint`** / **`formatter_available`** in the result.
- **`layout_scope`** (when **`auto_layout`** is on): **`ir_nodes`** (default) lays out only IR-touched nodes; **`full_graph`** calls **`LayoutEntireGraph`** on the whole target graph—use when the graph is messy outside your IR patch.
- **`blueprint_format_graph`:** readability-only pass: **`LayoutEntireGraph`** on a chosen script graph (`blueprint_path`, optional `graph_name`). Use after manual edits or when you did not want **`layout_scope: full_graph`** on apply.
- **`blueprint_compile`** accepts **`format_graphs: true`**: runs the formatter on **all** non-empty ubergraph, function, and macro graphs **before** compile—good after large multi-graph changes so you do not call **`blueprint_format_graph`** once per graph.

### Result fields to respect

- Successful apply returns **`merge_policy_used`**, **`anchors_reused[]`**, optional **`merge_warnings[]`**, **`layout_applied`**, **`layout_scope_used`**, and formatter status fields when present.
- If apply fails with structured **`errors[]`**, patch only the failing IR fields and retry once.

### IR shape

- Node references must be stable **`node_id`**; links must be **string pin refs** in **`node_id.pin`** form (example: `{ "from":"beginplay.Then", "to":"delay.execute" }`).
- Ops: `event_begin_play`, **`event_tick`**, `custom_event`, `branch`, `sequence`, `call_function`, `delay`, `get_variable`, `set_variable`, `dynamic_cast`.
- For **`call_function`**, always provide **`class_path`** + **`function_name`**.
- Do **not invent pseudo-ops** (for example `launch_character`, `play_sound`, `event_overlap`, `play_sound_at_location`, `add_component`). If the intent is gameplay behavior, express it as a supported op, usually **`call_function`**.
- Use canonical overlap event op names from the list above (for overlap flows, use `event_actor_begin_overlap`).
- For **`call_function.class_path`**, use native script class paths (typically `/Script/...`), not `/Game/...` asset object paths.
- **Packaging:** editor tools operate on the **project workspace**; packaging, staging, and platform cook steps are **out of scope** for this catalog unless a dedicated tool exists—direct the user to **Platforms** / **Project Launcher** when they ask for shipping builds.
