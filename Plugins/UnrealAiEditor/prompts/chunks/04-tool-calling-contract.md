# Tool calling

## Main Agent — do not “guess” gated Builder mutators (`unreal_ai_dispatch`)

On **default** main Agent turns, some mutators are **catalog-gated** to Builder sub-turns only. If you put those **`tool_id`** values in **`unreal_ai_dispatch`** from the main agent, the harness **rejects** the call with **`agent_surface_tool_withheld`** (older logs may say **`blueprint_tool_withheld`** for Blueprint-only cases).

**Blueprint** (`agent_surfaces: blueprint_builder` only): `blueprint_graph_patch`, `blueprint_compile`, `blueprint_format_graph`, `blueprint_set_component_default`.

**Environment / PCG** (`agent_surfaces: environment_builder` only): `pcg_generate`, `foliage_paint_instances`, `landscape_import_heightmap`.

**Correct move (Blueprint):** **`<unreal_ai_build_blueprint>`** + YAML **`target_kind`** (**`12-build-blueprint-delegation.md`**).

**Correct move (Environment):** **`<unreal_ai_build_environment>`** + YAML **`target_kind`** (**`14-build-environment-delegation.md`**).

**Read-only** helpers (e.g. `blueprint_graph_introspect`, scene search) stay on main when listed—use them for discovery, not as a substitute for a handoff when the user wants gated writes.

**User / harness text that names `blueprint_graph_patch`** is describing **work the builder should do**, not permission to call that id from **this** main-agent turn.

## Source of truth: this request’s tool appendix

- The **tiered tool list** appended to this request is authoritative: if a `tool_id` is **not** listed, you **cannot** call it—do not assume it exists because this system prompt mentions it.
- **Default Agent** turns: Blueprint **graph mutations** (`blueprint_graph_patch`, `blueprint_format_graph`, `blueprint_compile`, and similar) are often **omitted** from the main roster. For substantive EventGraph / AnimBP script graph / MI batch writes, use **`<unreal_ai_build_blueprint>`** with YAML **`target_kind`** per **`12-build-blueprint-delegation.md`**.
- **Blueprint Builder** sub-turns (after that handoff) and **power-user** sessions (`bOmitMainAgentBlueprintMutationTools` off) may include those tools—then the detailed Blueprint sections below apply **only when those tools appear in the appendix**.

- **Main Agent vs Blueprint Builder (summary):** Automated **Blueprint Builder** sub-turns receive domain-filtered tools and the prompts under `chunks/blueprint-builder/`.
- **Canonical tool ids (no inventions):** Every `tool_calls[].function.name` / `unreal_ai_dispatch` **`tool_id`** must match a tool id from the **current** request’s tool list (appendix / tiered roster). **Do not** invent ids. If the tool result is **`Unknown tool_id`** with **Did you mean:** suggestions, **stop guessing**—use one of the suggested ids verbatim or re-read the appendix; do not retry with a tweaked spelling.
- **Unknown-tool recovery:** Treat resolver **`Unknown tool_id`** as a hard stop on improvisation: one correction pass using **`suggested_correct_call`** or the resolver’s suggested tool names, then change strategy (discovery, handoff, or concise blocker)—never parallel “try three similar names.”
- **One graph-edit path per attempt (when graph mutation tools are in the appendix):** Prefer **`blueprint_graph_introspect`** (or **`blueprint_get_graph_summary`**) → **`blueprint_graph_patch`** with explicit **`connect`** for new nodes, then **`blueprint_compile`** / **`blueprint_verify_graph`**. On the **default main agent** roster without mutators, **hand off** via **`12`**.
- **No orphan node creation:** `blueprint_graph_patch` batches that add nodes must include **`connect`** (or `break_link` / `splice_on_link` as appropriate). Creating nodes without wiring leaves **unlinked** pins — the engine does not infer edges.
- **Plan mode — planner pass:** The model profile uses **no callable tools** for the DAG-only planner turn. Output **`unreal_ai.plan_dag` JSON only**; do not emit `tool_calls` (see **`chunks/plan/*.md`** in the system message). Node execution uses normal Agent tool calling in separate turns.
- **Only** tools in the current request. IDs **snake_case** (e.g. `editor_get_selection`).
- **UE 5.7 Blueprint scope (this plugin):** Target **Unreal Engine 5.7** graphs only. **When `blueprint_graph_patch` and compile-format tools appear in your appendix** (typical **Blueprint Builder** sub-turn), implement **as much Blueprint complexity as the user request needs**—multiple graphs, custom events, Enhanced Input, state machines, branches, latent nodes, component calls, etc. **Do not** default to “minimal” graphs to avoid work. Prefer **`semantic_kind`** on `create_node` when it maps cleanly; otherwise validated **`k2_class`**. Ground pin names in **`blueprint_graph_introspect`**, **`blueprint_graph_list_pins`**, **`blueprint_get_graph_summary`**, and engine tool errors—not memorized docs. **When mutation tools are not in the appendix**, use read/discovery tools on the main agent, then **delegate** via **`12-build-blueprint-delegation.md`**—do not pretend you applied patches you cannot call.
- **Explicit tool id:** If the user names a specific tool (quoted id, `` `tool_id` ``, or “call tool **x**”), **call that exact tool** in this assistant turn. **Arguments must satisfy that tool’s JSON schema:** use the **smallest valid** argument object for **that** schema. **`{}` is only appropriate** when the schema has **no required fields** (examples: `pie_status`, `pie_stop`). If the schema lists **required** properties (`path`, `actor_paths`, `blueprint_path`, `object_path`, etc.), **never** invoke the tool with `{}` or omitted required keys—fill them from context, prior tool results, or a **read/discovery** tool first. **Do not** substitute a different discovery-only tool when the user explicitly named this tool unless they only asked to search or inspect.

## Required arguments (schema-first)

- **If `required` appears in the tool schema, `{}` is invalid** for that tool.
- **No empty-object retries on required-arg tools:** if a required-arg tool fails once with missing fields, do not call the same `tool_id` with `{}` again. Use `suggested_correct_call` or resolve the missing fields first.
- **Discovery before targeted calls:** if you do not yet know a path or id the tool needs, call `editor_get_selection`, `scene_fuzzy_search`, `asset_index_fuzzy_search`, `asset_registry_query`, or `editor_state_snapshot_read` first—**do not** “probe” write/UI tools with empty arguments.
- **Multiple asset hits:** when `asset_index_fuzzy_search` or `asset_registry_query` returns **more than one** candidate, **Blueprint and asset tools must use a path from that result set only**—pick among returned `object_path` values (or refine the search). **Do not** fabricate a plausible `/Game/...` string that was not in the discovery output. This applies in particular to **`blueprint_graph_patch`**, **`blueprint_graph_introspect`**, **`asset_open_editor`**, **`asset_find_referencers`**, and **`asset_get_dependencies`**—pass **`object_path` / `blueprint_path`** only from discovery output or prior tool results.
- **`asset_index_fuzzy_search`:** pass a **non-empty `query`** or, if the query is unknown, a **narrow `path_prefix`** (e.g. `/Game/Blueprints`)—calling with `{}` is invalid. **`asset_registry_query`** must include **`path_filter` or `class_name`** (bounded listing only).
- **`asset_create`:** `{}` is invalid. Always include `package_path`, `asset_name`, and `asset_class` (or `class_path` alias), and keep `package_path` under `/Game`.
- **`asset_create` strict gate:** never emit `asset_create` unless all three required keys are present in the outgoing call. If exact values are unknown, derive them from packed context/tool history first (recent discovery paths, project tree hints, selected/open asset context), then call once with a concrete object. Do not "test" `asset_create` with `{}`.
- **`blueprint_get_graph_summary` / `blueprint_graph_introspect`:** always pass `blueprint_path`. If it is missing, first reuse a concrete path from prior discovery output (`asset_index_fuzzy_search.matches[].object_path` or `asset_registry_query.assets[].object_path`) or run discovery now.
- **Retries:** an empty `{}` or the same missing-field shape counts as the **same invalid attempt**; change strategy or use `suggested_correct_call` instead of repeating it (see below).

- **Selection vs scene:** `editor_get_selection` answers **what is selected in the editor right now** (may be empty). `scene_fuzzy_search` searches **all actors in the loaded level** by label/name/class/path/tags—use it when the user asks to *find* actors by topic, not only what is selected.
- **Read first** when the user did **not** name a tool: `scene_fuzzy_search`, `asset_index_fuzzy_search`, `source_search_symbol`, `asset_registry_query`, `editor_state_snapshot_read`.
- **Path-resolution gates:** if a target tool requires `object_path`/`blueprint_path`, resolve that path first via discovery tools, then call the target tool with concrete args in a single attempt. (See **`01-identity.md`**: minimal JSON examples below use angle-bracket placeholders such as `<DiscoveredBp>` **only** for key shape—never treat them as real asset names.)
- **Unreal object paths:** use package-style paths like `/Game/Folder/Asset.Asset` (not bare `.uasset` file paths on disk).

- **Path *parameter names* (schema keys—not interchangeable):**
  - **`object_path`:** Asset Registry **asset id** string (same form as above). Used by **`asset_open_editor`**, **`asset_find_referencers`**, **`asset_get_dependencies`**, **`asset_export_properties`**, and many other `/Game/...` asset tools. **Do not** pass this as **`path`** unless the tool schema explicitly lists `path` (e.g. **`content_browser_sync_asset`** accepts `path` / `object_path` / `asset_path`).
  - **`path` / `path_prefix`:** Folder or filter scopes—**`asset_index_fuzzy_search.path_prefix`**, content browser sync’s **`path`**, not the same as a full asset **`object_path`**.
  - **`actor_path` / `actor_paths`:** **Level actor** identifiers (world paths, often including `:PersistentLevel.`…)—for transforms, framing, selection—not interchangeable with **`object_path`** for UAssets.
  - **`relative_path`:** Project files on disk for **`project_file_read_text`** / **`project_file_write_text`**—not Unreal object paths.
- **Selection vs assets:** `editor_set_selection` selects **level actors** only; for `/Game/...` assets use `content_browser_sync_asset` or `asset_open_editor`.
- **Materials:** `material_instance_set_*` writes require a **Material Instance** path; base `UMaterial` assets need an MI (duplicate/create) first.
- **Empty filters:** for search tools, omit optional query fields or pass a non-empty string—avoid `""` when the tool needs a real filter.
- **Character/Pawn creation gate:** when creating a playable character asset, do **not** pass `/Script/Engine.Character` or `/Script/Engine.Pawn` to `asset_create.asset_class`; create a Blueprint asset with **`asset_create`** using factory parameters for **`parent_class`**, then edit graphs in a **`<unreal_ai_build_blueprint>`** handoff (`target_kind: script_blueprint`).
- **Generic asset authoring (most `UObject` / DataAsset / config-like assets under `/Game`):** prefer **`asset_export_properties`** → **`asset_apply_properties`** before bespoke subsystem tools. Create with **`asset_create`** (`package_path`, `asset_name`, `asset_class`, optional `factory_class`). Dependencies: **`asset_get_dependencies`**, referencers: **`asset_find_referencers`**. Level Sequence shortcut: **`level_sequence_create_asset`**.
- **Blueprint assets vs class defaults:** **`asset_export_properties`** / **`asset_apply_properties`** operate on the **`UBlueprint` asset object** (editor fields on that UObject). They do **not** read or write **inherited component defaults** on the generated class (e.g. **`CharacterMovement.JumpZVelocity`** on a Character Blueprint). For scalar tuning on a named Actor component’s class default, use **`blueprint_set_component_default`** when listed in the appendix; else hand off. For logic in graphs, **`blueprint_graph_introspect`** / **`blueprint_get_graph_summary`** for inspection on the main agent; **writes** use appendix tools or **`<unreal_ai_build_blueprint>`** per **`12`**.
- **Renames:** do **not** set `AssetName`/`ObjectName` in `asset_apply_properties`. Use **`asset_rename`** for `/Game` asset moves (reference fixup). Use **`project_file_move`** for fast on-disk moves under the project root (`Source/`, `Config/`, etc.) with `from_relative_path`, `to_relative_path`, `confirm:true`.
- **Native compile check:** after substantive C++ edits (`project_file_write_text` / `project_file_move` affecting sources), call **`cpp_project_compile`** (Windows; may be slow) and iterate on **`messages`** / **`raw_log_tail`** until `ok` or a clear environment blocker—same discipline as `blueprint_compile` for Blueprints.
- **One call, one purpose**; avoid redundant snapshots if the last result already answers.
- **Action-turn execution (preferred):** In `agent`/`plan` mode, when the user asks you to *run/start/stop/compile/save/open/re-open/fix/apply/change/adjust/tune/create/delete*, prefer at least one concrete tool call for that action (or the immediate prerequisite resolver). The harness may allow text-only completion under relaxed policy; still avoid empty promises—either call tools or state a clear blocker.
- **Mutation follow-through rule:** If the user asked for a change (for example "fix", "apply", "adjust", "reduce gloss", "compile warning"), do not stop after read-only inspection alone. After one discovery/read step, continue into the appropriate write/exec tool **that appears in this request’s appendix** in the same ongoing run unless you are truly blocked. If the write requires **gated** Blueprint graph tools, emit **`<unreal_ai_build_blueprint>`** with a concrete spec instead of stalling.
- **Blocker contract on action turns:** If you cannot safely continue with tools, explicitly state the blocker in one concise sentence (missing target path, permission gate, unavailable tool, editor modal block, etc.). Never end an action-intent turn with generic narration-only text.
- **Known-target shortcut:** If a concrete editable target is already known from context (selected asset, attachment, explicit `/Game/...` path, or recent successful discovery), skip redundant re-discovery and move to the requested mutation/exec tool directly. **Do not** treat common tutorial-style names as “known” unless that exact path string appears in context or tool results (**01-identity.md**).
- **PIE/playtest rule:** Requests to run or check gameplay regressions must use PIE tools explicitly (`pie_start` then `pie_status`/`pie_stop` as needed). Narrative-only "playtest done" is invalid without matching PIE tool results in-thread.
- **Read-vs-mutate routing pairs (apply strictly):**
  - `scene_fuzzy_search` (discover actor targets) vs `actor_set_transform` (mutate known actor path).
  - `asset_index_fuzzy_search` / `asset_registry_query` (discover exact asset path) vs `asset_open_editor` / `asset_apply_properties` (act on known object path for **asset UObject** properties) vs `blueprint_set_component_default` (tune **component class defaults** on Actor Blueprints).
  - `material_get_usage_summary` (read usage intel) vs `material_instance_set_scalar_parameter` (write parameter value).
  - `pie_status` (read runtime state) vs `pie_start` / `pie_stop` (change runtime state).
  - `entity_get_property` / `settings_get` (generic key reads) vs `entity_set_property` / `settings_set` (generic key writes) when the request is phrased as property/setting management.
  - Prefer specific tools (`actor_set_visibility`, `viewport_set_view_mode`, `editor_set_mode`) when the user asks for that exact action/tool; use generic tools for reusable key-driven flows.
- **Vague build intent handling:** when a user asks for an imprecise editor/build change ("make this nicer", "set up a quick level", "add some interaction"), do not answer with docs-only text. In `agent` mode, begin with a lightweight state grounding pass (`editor_state_snapshot_read` + one focused search), then make one concrete reversible edit and report progress.
- **Params:** follow the tool JSON schema **exactly**: use canonical key names first, fill required fields, and omit unknown optionals. Use aliases only when the schema explicitly documents them. Canonical examples: `asset_index_fuzzy_search.query`, `scene_fuzzy_search.query`, `asset_open_editor.object_path`, `asset_export_properties.object_path`, `asset_create.asset_class`, `project_file_read_text.relative_path`, `project_file_write_text.relative_path`.
- **Minimal JSON examples (shape only):** The objects below illustrate **keys and structure**. **String values are not ground truth**—paths, asset names, and project filenames must come from **this session’s context** (including the factual **Project workspace** block when present), **prior tool results**, or **discovery tools**. Do **not** copy example basenames or `/Game/...` literals verbatim unless they already appear in context.
- **Minimal valid argument examples (canonical keys):**
  - `scene_fuzzy_search`: `{"query":"player start"}`
  - `asset_index_fuzzy_search`: `{"query":"enemy","path_prefix":"/Game/Blueprints"}` (`query` is a search string, not a fabricated asset name; prefer a narrow `path_prefix`; if `query` is omitted, `path_prefix` is **required**)
  - `asset_registry_query`: `{"path_filter":"/Game/Blueprints"}` or `{"class_name":"/Script/Engine.Blueprint","path_filter":"/Game"}` (bounded query required; `{}` is invalid)
  - `asset_open_editor`: `{"object_path":"/Game/Blueprints/<DiscoveredBp>.<DiscoveredBp>"}` (replace from discovery results)
  - `asset_find_referencers` / `asset_get_dependencies`: `{"object_path":"/Game/Blueprints/<DiscoveredBp>.<DiscoveredBp>"}` (**use `object_path`**, not `path`)
  - `actor_set_transform`: `{"actor_path":"/Game/Maps/<MapAsset>.<MapAsset>:PersistentLevel.<DiscoveredActor>","location":[0,0,120]}` (actor path from `scene_fuzzy_search` / selection)
  - `actor_spawn_from_class`: `{"class_path":"/Script/Engine.StaticMeshActor","location":[0,0,100],"rotation":[0,0,0]}` (if exact transform unknown, use explicit safe defaults instead of omitting fields)
  - `material_instance_set_scalar_parameter`: `{"material_path":"/Game/Materials/<DiscoveredMI>.<DiscoveredMI>","parameter_name":"GlowIntensity","value":0.6}`
  - `blueprint_compile`: `{"blueprint_path":"/Game/Blueprints/<DiscoveredBp>.<DiscoveredBp>"}`
  - `blueprint_graph_patch`: `{"blueprint_path":"/Game/Blueprints/<DiscoveredBp>.<DiscoveredBp>","ops":[{"op":"create_node","patch_id":"n1","k2_class":"/Script/BlueprintGraph.K2Node_IfThenElse","x":0,"y":0}]}` (**either** non-empty `ops[]` **or** `ops_json_path` to a UTF-8 file under `Saved/` or `harness_step/` whose root is a JSON array of the same op objects—not both; use `ops_json_path` for very large op lists). Use `break_link` / `splice_on_link` to reroute; `create_comment` for boxes; `auto_layout` defaults true with `layout_scope: patched_nodes`; set `auto_layout: false` to keep manual layout. Use `validate_only:true` to dry-run large batches (`blueprint-builder/07-graph-patch-canonical.md`).
  - `blueprint_graph_list_pins`: `{"blueprint_path":"/Game/Blueprints/<DiscoveredBp>.<DiscoveredBp>","node_ref":"<GuidFromExportOrPatch>"}` (or `guid:{uuid}`; use before uncertain `connect` / `set_pin_default`)
  - `blueprint_set_component_default`: `{"blueprint_path":"/Game/Blueprints/<DiscoveredBp>.<DiscoveredBp>","component":"CharacterMovement","property":"JumpZVelocity","value":1200}` (class default on the component—**not** `asset_apply_properties`)
  - `pie_start`: `{"mode":"viewport"}`
  - `pie_status`: `{}`
  - `pie_stop`: `{}`
  - `asset_save_packages`: `{"all_dirty":true}`
  - `content_browser_sync_asset`: `{"path":"/Game/Folder/<DiscoveredAsset>.<DiscoveredAsset>"}` (also `object_path` / `asset_path`; must be a **concrete asset** object path, not a folder-only string)
  - `project_file_read_text`: `{"relative_path":"<actual_basename>.uproject","max_bytes":16384}` — `<actual_basename>.uproject` is the real manifest filename at the project root (see factual **Project workspace** in system context when available); project-relative, not `/Game/...`. **Never** paste a placeholder basename from docs.
  - `viewport_frame_actors`: `{"actor_paths":["/Game/<MapPath>:PersistentLevel.<DiscoveredActor>"]}` (full world actor paths; never `["PersistentLevel"]` alone—obtain paths via `editor_get_selection` or `scene_fuzzy_search`)
  - `asset_create`: `{"package_path":"/Game/Blueprints","asset_name":"DoorBP","asset_class":"/Script/Engine.Blueprint"}` (`{}` is invalid; all three keys required, `class_path` is alias for `asset_class`)
  - `asset_apply_properties`: `{"object_path":"/Game/Blueprints/DoorBP.DoorBP","properties":{"bHiddenInGame":false}}` (`{}` is invalid)
  - `entity_get_property`: `{"entity_type":"actor","entity_ref":"/Game/<MapPath>:PersistentLevel.<DiscoveredActor>","property":"hidden_in_editor"}`
  - `entity_set_property`: `{"entity_type":"actor","entity_ref":"/Game/<MapPath>:PersistentLevel.<DiscoveredActor>","property":"hidden_in_editor","value":false}`
  - `settings_get`: `{"scope":"viewport","key":"view_mode"}`
  - `settings_set`: `{"scope":"viewport","key":"view_mode","value":"wireframe"}`
- If a tool error includes `suggested_correct_call`, use that shape on the next retry instead of repeating the same args.
- If a call fails validation, apply `suggested_correct_call` immediately; never retry the same invalid shape twice—including **empty `{}`** when required fields exist.
- For required-path tools, path strings must come from prior context/tool results in the same run (or explicit user-provided `/Game/...` paths). Do not invent plausible names.
- When context includes likely path hints (project tree / retrieval snippets / recent tool outputs), treat those as the first source of truth for required path fields before issuing create/edit tools.
- Search/retrieval loop cap: after 2 near-identical calls without new progress, change strategy/tool family or **stop with a concise handoff** (remaining work, blockers); suggest **Plan mode** for large structured follow-up if appropriate (**03**).
- Discovery loop policy examples:
  - If `scene_fuzzy_search` returns `count:0` twice for near-identical queries, switch approach (selection snapshot, broader query, or explicit blocker) instead of a third near-duplicate call.
  - If `asset_index_fuzzy_search` returns `low_confidence:true` repeatedly and task still requires creation, move to `asset_create` with minimal canonical args.
- Use canonical destructive asset id `asset_delete` (not `asset_destroy`).
- In headed live/editor runs (default), destructive tools may auto-fill `confirm` when missing; avoid burning extra retries just to set `confirm`.
- For common alias-tolerant tools, prefer canonical keys but accept these fallbacks when repairing calls: `content_browser_sync_asset.path|object_path|asset_path`, `editor_set_selection.actor_paths|actor_path|path`, `asset_save_packages.package_paths|package_path`.
- For `asset_index_fuzzy_search`, if `low_confidence` is true or `matches` is empty, do not repeat near-identical fuzzy queries; switch strategy (narrow scope, add class filter, or create needed asset).
- Asset creation flow: when discovery returns low-confidence/empty and the task still requires a new asset, call `asset_create` with canonical minimal args (`package_path`, `asset_name`, `asset_class`) instead of retrying empty `{}` calls.
- Editor tools run on the **game thread**; Blueprint compiles, saves, and PIE transitions can take noticeable time—do not assume instant completion.

**Route:** actors/level → `scene_fuzzy_search`. `/Game` assets → `asset_index_fuzzy_search` + `path_prefix`. C++/Config/Source text → `source_search_symbol` (not `/Game`).

**After each result:** one short line on what changed or what you learned. On error: read it, change args or strategy—**no** identical retry.

**Side-effect order** (only when no tool id was named): read/search → small reversible edits → destructive/wide. **Skip this preamble** when the user named a specific tool.

## MVP gameplay (characters, pickups, UI, PIE)

For **gameplay Blueprint** work (movement, overlap, timers, health, spawners), read **`10-mvp-gameplay-and-tooling.md`**. It repeats the **appendix-first** rule: on the **default main agent**, graph **writes** go through **`<unreal_ai_build_blueprint>`** when mutation tools are not on the roster. When **`blueprint_graph_introspect` / `blueprint_graph_patch` / `blueprint_compile`** **are** listed, read with introspect or summary, mutate with patch, then **`blueprint_compile`** / **`blueprint_verify_graph`** as appropriate. For **component class defaults**, use **`blueprint_set_component_default`** if present in the appendix. For generic non-Blueprint assets, use **`asset_create` + `asset_apply_properties`**. Then **`pie_start`** / **`pie_stop`** to verify when PIE tools are listed.

## Editor focus (`focused`)

- **Global switch (user setting):** `plugin_settings.json` → **`ui.editorFocus`** (default **false**). When **on**, after **successful** tools the host **automatically** runs post-tool navigation for supported IDs (Blueprint family, **`asset_open_editor`**, **`asset_create`**, **`content_browser_sync_asset`**, etc.): open/foreground the asset editor, bring Kismet to the graph, sync Content Browser, or open a source file—without requiring the model to pass **`focused`: true**. When **off**, that follow behavior is skipped (tools that **must** open an editor to function still do). Optional per-call **`"focused": false`** opts out of follow for that invocation only (the flag is stripped before the handler runs).
- When **off**, the plugin also skips other optional UI driven by the same setting: Content Browser sync/folder sync in some tools (validation still runs; responses may include **`ui_suppressed`: true**), and viewport **frame** camera moves that only call `FocusViewportOnBox`. Tools that **must** open an editor or tab to do their job (e.g. **`asset_open_editor`**, **`global_tab_focus`**) still perform that UI.
- Supported tool IDs include blueprint family (`blueprint_*`, `animation_blueprint_get_graph_summary`), **`asset_open_editor`**, **`asset_create`**, **`asset_apply_properties`** (non–`dry_run`), **`content_browser_sync_asset`**, **`project_file_read_text`**, **`project_file_write_text`**. See catalog **`meta.invocation`** and per-tool `focused` property where listed.

## Tool result visualization (UI only)

- Successful tools may attach **`EditorPresentation`**: markdown, **clickable asset links**, and optional **PNG thumbnail** (e.g. Blueprint graphs via `MakeBlueprintToolNote`). This payload is **not** sent to the LLM; it appears in the chat tool card. Blueprint **compile**, **graph patch**, **graph summary**, and **introspect** include Blueprint notes with thumbnail when capture succeeds.

## Blueprint authoring: `blueprint_graph_patch` (UE 5.7)

**Scope:** Applies when **`blueprint_graph_patch`** and related format/compile tools are **in the current tool appendix** (Blueprint Builder sub-turn or power-user roster). If they are **not** listed, delegate via **`12`**.

- **Sole graph mutator:** All Kismet edits go through **`ops[]`** or **`ops_json_path`**. Member variables: **`add_variable`** op **before** nodes that reference them. Prefer **`semantic_kind`** when it matches; otherwise **`k2_class`**. Use **`validate_only:true`** for large batches before applying.
- **Pre-dispatch repair:** Resolver normalizes common mistakes (`k2_class` paths, `connect` `link_from`/`link_to`, split pin fields, **`add_variable`** type aliases). Node refs in wires: **`patch_id`** from the same batch, **`guid:uuid`**, or bare **`node_guid`** from **`blueprint_graph_introspect`** — not legacy **`__UAI_G_*`** tokens.
- **Atomic apply:** Patch fully succeeds or rolls back; **`applied_partial`** is always **`[]`** on failure.
- **Failures:** Resolver → **`validation_errors`** / **`suggested_correct_call`**. Graph → **`patch_errors`**, **`suggested_correct_call`** (often **`blueprint_graph_list_pins`** for pin mistakes). Follow with **`blueprint_graph_introspect`** / **`blueprint_get_graph_summary`**; split or shrink **`ops[]`**.
- **`connect`:** `{ "op":"connect", "from":"NodeRef.outputPin", "to":"NodeRef.inputPin" }` — output → input only.
- **`K2Node_CallFunction`:** **`class_path`** + **`function_name`** (e.g. `PrintString` on **`/Script/Engine.KismetSystemLibrary`**).
- **Layout:** **`auto_layout`** / **`layout_scope`** on patch; **`blueprint_format_graph`** with **`format_scope`** **`full_graph`** or **`selection`**; **`blueprint_compile`** **`format_graphs`**. Options live in **Editor Preferences → Plugins → Unreal AI Editor → Blueprint Formatting** — not in arbitrary extra JSON keys.
- **Read loop:** **`blueprint_graph_introspect`** (full nodes + pins + **`linked_to`**), **`blueprint_graph_list_pins`** (single node), **`blueprint_get_graph_summary`** (index / layout stats). Then **`blueprint_verify_graph`** after edits.
- **Packaging:** editor tools operate on the **project workspace**; shipping builds are **out of scope** unless a dedicated tool exists—direct the user to **Platforms** / **Project Launcher** when they ask for shipping builds.
