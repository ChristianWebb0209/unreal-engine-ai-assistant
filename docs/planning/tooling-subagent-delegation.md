# Tooling subagent delegation — richer catalogs via narrow surfaces

This document proposes **replacing the wide single-agent tool path** with **mandatory specialist subagents**: each LLM turn carries a **small, stable tool roster** and **longer domain guidance**, so the catalog can grow in **more tools**, **richer JSON schemas**, and **deeper descriptions** without sharing one overstuffed context.

### Rebuild posture (not a compatibility layer)

- **Greenfield target** — one supported product shape: **thin orchestrator + delegated specialists**. There is **no** parallel “legacy wide main agent” mode to maintain.
- **Implementation may reuse files and types** (`agent_surfaces`, harness hooks, chunk folders) as **mechanical scaffolding** while behavior and defaults are **re-cut** to match this doc.
- **Breaking changes are expected** — settings, defaults, and which tools appear on which turn will change; migrations are **one-way** (document in release notes / upgrade guide separately).

**Related (current architecture — reference only):**

- Tool registry narrative + dispatch / tiered eligibility: [`docs/tooling/tool-registry.md`](../tooling/tool-registry.md), [`docs/tooling/tools-expansion.md`](../tooling/tools-expansion.md).
- Catalog sources: `Plugins/UnrealAiEditor/Resources/tools.main.json` (+ `tools.blueprint.json`).
- Builder handoffs: `<unreal_ai_build_blueprint>` in harness (`FUnrealAiAgentHarness.cpp`) and tag parser (`UnrealAiBuildBlueprintTag`).
- Blueprint builder domain enum: `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Public/UnrealAiBlueprintBuilderTargetKind.h`.
- Surface gating: `tools[].agent_surfaces`, `UnrealAiAgentToolGate`, `UnrealAiToolSurfaceCompatibility`.
- Plan-mode parallelism (separate axis): `agent.useSubagents` — plan **wave** workers, not the product “builder” sub-turns described here.
- Prompt composition rules and assembly map: [`Plugins/UnrealAiEditor/prompts/README.md`](../../Plugins/UnrealAiEditor/prompts/README.md) (`FUnrealAiLinearPromptAssemblyStrategy`).

**Non-goals (this plan):** replacing the C++ tool dispatcher; defining every new tool ID; changing provider APIs.

---

## 1. Problem statement

Today, the default path combines:

1. **Dispatch mode** — compact native `tools[]` plus a **markdown tool index** in the prompt (`UnrealAiTurnLlmRequestBuilder`).
2. **Tiered eligibility** — BM25 + domain bias + session prior + dynamic **K** (`UnrealAiToolSurfacePipeline`), so the **visible roster varies by turn**.
3. **A wide main agent** — discovery, navigation, world edits, assets, settings, PIE, diagnostics, etc., share one surface; graph/environment work uses ad hoc handoffs rather than a uniform specialist model.

That design optimizes **payload size** and **broad relevance**, but works against:

- **Repeatability** — the same user intent may surface different tool subsets.
- **Instruction density** — there is limited room for long “how to use this tool family” prose when hundreds of tools compete for appendix budget.
- **Schema granularity** — splitting one coarse tool into several precise tools increases **choice overload** unless the active set is small.

**Hypothesis:** More **delegation rounds** + **narrow subagent contexts** improve accuracy and allow **tool catalog growth** without proportional growth in **per-call confusion**.

---

## 2. Design principles

1. **Thin orchestrator** — The user-facing agent primarily **routes**, **briefs**, and **summarizes**. It exposes only **routing tools** and **read-mostly** context tools unless a product requirement forces otherwise.
2. **One specialist, one budget** — Each specialist turn targets a **character budget** for tool index + instructions (aligned with `meta.optimization` in the catalog, e.g. `tiered_appendix_char_budget`, `tool_surface_budget_chars`).
3. **Stable allow-lists** — Specialist turns should prefer **fixed or pinned** tool sets (minimal dynamic K / optional “core pack only”) so behavior is reproducible.
4. **Orthogonal taxonomy** — Organize subagents by **domain** and **mutation class** (scout vs mutator vs verifier), not by LLM model.
5. **Prefer clear data over legacy behavior** — Catalog metadata (`agent_surfaces`, fragments, `target_kind`, manifests) should express the **new** model; retire flags and code paths that existed only to preserve the old wide-agent experience.

---

## 3. Organization model

### 3.1 Layers

| Layer | Purpose | Typical tools |
|-------|---------|----------------|
| **L0 — Orchestrator** | Intent, safety, delegation, user-visible reply | Routing handoffs, optional snapshot read |
| **L1 — Scout (read)** | Discovery, introspection, structured brief for downstream | Search, introspect, registry query, graph read |
| **L2 — Mutator (write)** | Apply patches, spawn actors, asset ops | Domain-restricted write tools |
| **L3 — Verifier (read)** | Compile, validate, logs, post-conditions | `*_verify*`, `*_compile`, message log, snapshot |

Scout → Mutator → Verifier can be **three LLM rounds** for high-risk graphs; simple tasks collapse to Mutator only.

### 3.2 Catalog alignment

Specialists map to existing **`meta.categories`** in `tools.main.json` (e.g. `world_actors`, `assets_content`, `blueprints`, `materials_rendering`, `viewport_camera`, …) and to **`agent_surfaces` tokens** (`main_agent`, `blueprint_builder`, `blueprint_builder`, `all`).

**New surfaces (proposed):** add tokens only when a specialist needs a **distinct allow-list** that would pollute `main_agent` or existing builders — e.g. `scene_specialist`, `asset_specialist`, `viewport_specialist`, `playtest_specialist`, `diagnostics_specialist`, `project_intel_specialist`, `router_only`. Exact names are implementation decisions; keep them **few and composable**.

### 3.3 Handoff envelope (conceptual)

Each delegation should carry a **machine-friendly brief** (YAML or JSON block), analogous to today’s builder tags:

- `specialist_id` — stable enum string.
- `goal` — one sentence.
- `constraints` — paths, forbidden ops, engine version assumptions.
- `artifacts` — `/Game/...` paths, actor paths, node GUIDs from scout.
- `stop_condition` — e.g. “compile clean”, “user-visible screenshot captured”.

The orchestrator **does not** pass wall-of-text tool docs; those live in **specialist system prompts** and **specialist-scoped catalog slices**.

---

## 4. Subagent catalog (detailed)

### 4.1 L0 — Orchestrator (main user-facing agent)

| Field | Detail |
|-------|--------|
| **Role** | Route user requests to specialists; maintain conversational continuity; never monopolize detailed tool usage. |
| **Allowed tools** | Minimal: e.g. `editor_state_snapshot_read`, optional `engine_message_log_read`, and **delegation / handoff** only. **No** broad `asset_*`, `actor_*`, graph mutators, or other specialist tools on the orchestrator turn. |
| **Prompt focus** | When to delegate, what must be in the brief, escalation if blocked. |
| **Outputs** | User-visible answer + structured handoff blocks for specialists. |

---

### 4.2 L1/L2 — Scene & level specialist

| Field | Detail |
|-------|--------|
| **Role** | Everything **in-world** for the loaded editor level: find actors, spawn/destroy, transforms, attachment, visibility, outliner folders. |
| **Primary categories** | `world_actors`, parts of `selection_framing` when tied to actor paths. |
| **Representative tools** | `scene_fuzzy_search`, `actor_find_by_label`, `actor_get_transform`, `actor_get_visibility`, `editor_get_selection`, `editor_set_selection`, `entity_get_property` (read-only scene inspection). |
| **Scout vs mutator** | Scout: search/find/read transforms. Mutator: spawn, destroy, transform, hierarchy. |
| **Delegation from** | Orchestrator when the user asks about “in the level”, “actor”, “scene”, placement. |

---

### 4.3 L1/L2 — Viewport & presentation specialist

| Field | Detail |
|-------|--------|
| **Role** | Camera control, framing, view modes, **screenshots** for vision / QA. |
| **Primary categories** | `viewport_camera`, `capture_vision`, `selection_framing` (viewport framing). |
| **Representative tools** | `viewport_camera_control`, `viewport_get_view_mode`, `viewport_set_view_mode`, `viewport_frame`, `viewport_capture`, `editor_set_selection` / `editor_get_selection` (if needed to establish framing targets). |
| **Notes** | Keep **capture** and **camera** together; users often interleave them. |

---

### 4.4 L1/L2 — Asset & content librarian specialist

| Field | Detail |
|-------|--------|
| **Role** | Content Browser navigation, asset CRUD, registry queries, fuzzy asset search, open editor, save packages, dependency graph queries, property export/apply at asset level. |
| **Primary categories** | `assets_content`, `editor_ui_navigation` (browser/sync/open). |
| **Representative tools** | `asset_index_fuzzy_search`, `asset_registry_query`, `asset_get_metadata`, `asset_graph_query`, `asset_create`, `asset_duplicate`, `asset_rename`, `asset_delete`, `asset_save_packages`, `asset_open_editor`, `content_browser_sync_asset`, `content_browser_navigate_folder`, `asset_export_properties`, `asset_apply_properties`. |
| **Scout vs mutator** | Scout: search/registry/metadata. Mutator: create/rename/delete/apply/save. |
| **Split** | **Level Sequence / Sequencer** (`animation_sequencer`) is **not** this lane — orchestrator delegates `specialist="animation"` (see **4.4a**). |

---

### 4.4a L1/L2 — Animation & sequencer specialist

| Field | Detail |
|-------|--------|
| **Role** | Level Sequence assets, opening Sequencer, AnimBlueprint graph summaries — catalog tools under **`animation_sequencer`**. |
| **Primary categories** | `animation_sequencer`. |
| **Representative tools** | `level_sequence_create_asset`, `sequencer_open`, `animation_blueprint_get_graph_summary` (availability follows catalog `status`). |
| **Delegation from** | Orchestrator when the user asks for cinematics, Level Sequence, Sequencer, or AnimBP graph readback in this category. |
| **Boundary** | Kismet **gameplay** Blueprint graphs stay on **`<unreal_ai_build_blueprint>`** (`target_kind` as today). |

---

### 4.5 L1/L2 — Editor UI & navigation specialist

| Field | Detail |
|-------|--------|
| **Role** | Modes, tabs, menu commands, global editor chrome — **not** asset content mutation. |
| **Primary categories** | `editor_ui_navigation`, `console_exec` (optional: keep console on a separate specialist if desired). |
| **Representative tools** | `editor_get_mode`, `editor_set_mode`, `global_tab_focus`, `menu_command_invoke`, optionally `console_command`. |

---

### 4.6 L2 — Settings & plugin configuration specialist

| Field | Detail |
|-------|--------|
| **Role** | Allow-listed settings reads/writes via the settings envelope. |
| **Primary categories** | `settings_properties`. |
| **Representative tools** | `setting_query`, `setting_apply`. |
| **Isolation rationale** | Reduces accidental settings drift mixed with scene/asset operations. |

---

### 4.7 L1/L2 — Playtest & PIE specialist

| Field | Detail |
|-------|--------|
| **Role** | Start/stop/status of Play-In-Editor; pair with diagnostics specialist for log checks. |
| **Primary categories** | `pie_play`. |
| **Representative tools** | `pie_start`, `pie_stop`, `pie_status`. |

---

### 4.8 L1 — Diagnostics & observability specialist

| Field | Detail |
|-------|--------|
| **Role** | Logs, deterministic editor snapshots, audit trail; often used as **L3 verifier** after mutations. |
| **Primary categories** | `diagnostics_logs`. |
| **Representative tools** | `engine_message_log_read`, `editor_state_snapshot_read`, `tool_audit_append` (if enabled). |

---

### 4.9 L1/L2 — Project files & source intelligence specialist

| Field | Detail |
|-------|--------|
| **Role** | Project-scoped file read/write (policy-bound), symbol search, optional C++ compile invocation. |
| **Primary categories** | `project_files_search`. |
| **Representative tools** | `project_file_read_text`, `project_file_move`, `source_search_symbol`, `cpp_project_compile` (with policy gates). |

---

### 4.10 L2 — Materials (instance) specialist

| Field | Detail |
|-------|--------|
| **Role** | Material Instance parameter tuning and readback; lightweight material usage questions. |
| **Primary categories** | `materials_rendering` (instance-focused). |
| **Representative tools** | `material_instance_set_parameter`, `material_instance_get_scalar_parameter`, `material_instance_get_vector_parameter`, `material_get_usage_summary`. |
| **Boundary** | No base material **expression graph** patches here — those go to **Material graph builder** (4.12). |

---

### 4.11 L1/L2/L3 — Blueprint Builder family (split by `target_kind`)

Each row is a **separate sub-turn profile** sharing the same harness hook but different **prompt chunks** and **tool allow-list**. Today’s enum (`EUnrealAiBlueprintBuilderTargetKind`) is the organizational spine.

| Subagent | `target_kind` | Role | Primary tools |
|----------|----------------|------|----------------|
| **K2 script Blueprint builder** | `script_blueprint` (default) | Event graph / functions: introspect, patch, format, compile, verify, component defaults | `blueprint_graph_introspect`, `blueprint_graph_list_pins`, `blueprint_graph_patch`, `blueprint_format_graph`, `blueprint_compile`, `blueprint_verify_graph`, `blueprint_set_component_default`, `blueprint_get_graph_summary` |
| **Animation Blueprint builder** | `anim_blueprint` | AnimBP graphs under same K2 patch stack where supported | Same graph stack + anim-specific prompt constraints |
| **Widget Blueprint builder** | `widget_blueprint` | UMG: K2 + designer limitations spelled out in prompt | Graph stack + widget-specific instructions |
| **Niagara Blueprint-related builder** | `niagara` | Niagara scripts/systems as exposed by your catalog | Niagara-facing tools as you add them |
| **Material Instance builder** | `material_instance` | If distinct from 4.10: MI-focused **asset** workflows inside builder handoff | Instance tools + open/sync |
| **Material graph builder** | `material_graph` | Base material expression graph ops | `material_graph_summarize`, `material_graph_export`, `material_graph_patch`, `material_graph_validate`, `material_graph_compile` |

**L3 verifier** for this family: `blueprint_verify_graph`, `blueprint_compile`, `material_graph_validate`, `material_graph_compile`, plus diagnostics tools.

---

### 4.12 L1/L2 — Environment & PCG builder (removed)

Procedural environment building (PCG, landscape, foliage) and the Environment Builder sub-turn were **removed** from the product. World actor placement is also out of scope — use the Unreal Editor for level layout.

---

### 4.13 L1 — Scout-only micro-agent (optional)

| Field | Detail |
|-------|--------|
| **Role** | **Read-only** pass: fuzzy search, introspection, registry; returns a **structured brief** for a mutator specialist. |
| **Typical tools** | `scene_fuzzy_search`, `asset_index_fuzzy_search`, `blueprint_graph_introspect`, `material_graph_summarize`, `editor_state_snapshot_read`. |
| **Benefit** | Shrinks mistaken writes; mutator sees only the brief + IDs. |

---

### 4.14 L1 — Graph patch planner micro-agent (optional)

| Field | Detail |
|-------|--------|
| **Role** | Emit **only** `ops[]` for `blueprint_graph_patch` or `material_graph_patch` (no execution). |
| **Next step** | Mutator specialist validates and applies, or execution host runs after human review (future). |
| **Benefit** | Separates **planning** from **execution** for fewer partial transactions. |

---

### 4.15 L0/L1 — Router micro-agent (optional, smallest context)

| Field | Detail |
|-------|--------|
| **Role** | Map user message → `specialist_id` + `target_kind` + required brief fields; **no tools** or only `editor_state_snapshot_read`. |
| **Benefit** | Cheaper routing; keeps orchestrator prompts short. |

---

## 5. What this unlocks for the catalog

With narrow surfaces per specialist, each can carry:

- **Longer `summary` + `failure_modes` + examples** in the merged JSON for tools in that surface only.
- **More granular `tool_id`s** (splitting coarse tools) because **K** and choice overload apply to a small set.
- **Stricter schemas** (`additionalProperties: false`) with **fewer resolver alias shims** — specialists learn one canonical shape per tool.

The **orchestrator** should use a **fixed micro-roster** (handful of tools — no dynamic K required). **Specialists** always use **pinned allow-lists**; any ranking inside that list is optional and bounded.

---

## 6. Repository, file layout, and prompt architecture

This section ties **delegation** to **concrete repo layout**: smaller files, clear ownership, and assembly rules that stay maintainable as the catalog grows.

### 6.1 Goals (anti–“1000-line file”)

1. **No new monoliths** — default merge target for tools stays **sharded by domain**; prompts stay **one idea per file** (existing rule in `prompts/README.md`).
2. **Machine-verifiable boundaries** — specialist allow-lists and chunk manifests should be **data** (JSON/TOML) or **small C++ tables**, not thousand-line `switch` bodies.
3. **Parallel structure** — the same specialist id should name: a **catalog slice**, a **prompt subtree**, optional **dispatch `.cpp`**, and **generated docs** — predictable navigation for humans and CI.

**Soft limits (targets, not hard law):**

| Artifact | Target |
|----------|--------|
| Prompt chunk `.md` | ~**80–250** lines; split when a file covers more than one behavioral contract |
| Merged `tools.*.json` fragment | ~**300–600** lines of `tools[]`; add a new fragment file rather than inflating one blob |
| C++ translation unit (dispatch / harness helper) | Prefer **domain files** (`UnrealAiToolDispatch_Foo.cpp`) over a single mega-router when a domain exceeds ~**400–800** lines of routing |

### 6.2 Tool catalog files (separation of concerns)

**Today:** `tools.main.json` + `tools.blueprint.json` merged in `FUnrealAiToolCatalog::LoadFromPlugin`; `meta.tool_catalog_fragments` lists fragments.

**Direction for specialists:**

1. **Add domain fragments** using the same merge mechanism — e.g. `tools.scene.json`, `tools.assets.json`, `tools.viewport.json`, `tools.diagnostics.json` — each owned by one team/concern. Keep **`meta`** (version, categories legend, optimization caps) in **`tools.main.json` only** or in a tiny `tools.meta.json` if you ever split meta out.
2. **Per-tool metadata** continues to drive surfaces: `agent_surfaces`, `retrieval_bundle`, `context_selector` (`blueprint_builder_core`, `blueprint_builder_core`, and future `specialist_core` flags). Avoid duplicating prose in JSON; link long narrative to `docs/tooling/` or `prompts/tools/specialists/`.
3. **Generated, not hand-curated at scale** — extend scripting (alongside `scripts/Validate-UnrealAiToolCatalog.ps1`) to emit:
   - per-specialist **allow-list** headers for C++ or JSON manifests;
   - `prompts/tools/catalog-snapshot.tsv` **slices** (e.g. `catalog-snapshot.scene.tsv`);
   - optional **Markdown indexes** per specialist for code review.

Large single-file pain in **`tools.main.json`** is addressed by **moving new tools into the smallest relevant fragment** and shrinking `main` to cross-cutting + highest-frequency tools only.

### 6.3 C++ and harness layout

1. **Dispatch** — follow the existing pattern of **domain-specific** implementation units (e.g. material graph dispatch separate from core router). New domains (scene, assets) get **`UnrealAiToolDispatch_<Domain>.cpp`** + declarations in a thin **`UnrealAiToolDispatch.cpp`** router that forwards by `tool_id` prefix or table lookup.
2. **Harness** — avoid growing `FUnrealAiAgentHarness.cpp` indefinitely: introduce focused types (naming illustrative):
   - `FUnrealAiSpecialistTurnScheduler` — queue/orchestrate scout → mutator → verifier rounds;
   - `FUnrealAiDelegationTagParser` — parse `<unreal_ai_delegate …>` similarly to `UnrealAiBuildBlueprintTag`;
   - keep **telemetry hooks** centralized.
3. **Request builder** — `UnrealAiTurnLlmRequestBuilder` should take a **`FUnrealAiToolSurfaceProfile`** (or extend existing context) naming `specialist_id`, **pinned tool ids**, and **appendix mode** (`tiered` vs `allow_list_only`) instead of encoding that logic only as booleans scattered across the harness.

### 6.4 Prompt chunks: directory layout

**Keep** the established pattern from `prompts/README.md`:

- **`prompts/chunks/common/`** — identity, modes, tool-calling contract, context, safety, output style (shared across all agents).
- **`prompts/chunks/blueprint-builder/`** + **`kinds/`** — builder sub-turn (already split by `target_kind`).
- **`prompts/chunks/`** + **`kinds/`** — environment sub-turn.
- **`prompts/chunks/plan/`** and **`plan-node/`** — DAG modes (orthogonal to product specialists).

**Add** parallel trees for new product specialists (mirror blueprint-builder structure):

```text
prompts/chunks/specialists/
  orchestrator/ # L0: routing, delegation contract
    00-overview.md
    01-delegation-envelope.md
    02-handoff-tags.md
  scene/
    00-overview.md
    01-discovery-vs-mutation.md
    02-safe-spawn-destroy.md
    kinds/ # optional sub-profiles (e.g. lighting_vs_geometry)
  assets/
    00-overview.md
    ...
  viewport/
    00-overview.md
    ...
  diagnostics/
    00-overview.md
    ...
```

**Naming:**

- **Numbered prefixes** (`00`, `01`, …) define a **stable composition order** within that specialist; kinds use **`kinds/<profile>.md`** when one specialist has multiple profiles (same pattern as Blueprint/Environment builders).
- **Delegation from main** prose for each specialist can live in **`specialists/<name>/08-delegation-from-main-agent.md`** (or `07-` if you want symmetry with env builder), loaded only on the **orchestrator** stack — mirroring `blueprint-builder/08-delegation-from-main-agent.md`.

### 6.5 Prompt assembly: avoid a giant strategy function

**Today:** `FUnrealAiLinearPromptAssemblyStrategy::BuildSystemDeveloperContent` sequences chunks with explicit `AppendChunk` / `AppendBlueprintBuilderChunk` calls (`UnrealAiPromptAssemblyStrategy.cpp`).

**Direction:**

1. **Assembly manifest** (e.g. `prompts/assemblies/orchestrator.json`, `specialist_scene.json`) listing ordered chunk paths under `prompts/chunks/`. The C++ strategy **loads and walks the manifest** for a given `EUnrealAiPromptStackId` (new enum) instead of hard-coding twenty more `if (bSceneSpecialist)` branches.
2. **Params-driven inclusion** — extend `FUnrealAiPromptAssembleParams` with `SpecialistId` / `PromptStack` and flags like `bScoutOnlyChunk` so one manifest row can be `condition: inject_if_scout`.
3. **Cache key** — `UnrealAiMainAgentPromptDiskCache` (and any builder cache) must include **stack id + specialist + kind** so cached prefixes stay correct.

Update **`prompts/README.md`** “Canonical assembly map” whenever manifests or load order change (single source of truth for authors).

### 6.6 Tooling docs beside prompts (`prompts/tools/`)

- **`by-category.md`** — remain **generated** from merged catalog; optionally emit **`by-specialist/<id>.md`** for reviewer-friendly views.
- **`core-pack.md`** — split or parameterize: e.g. **`core-pack.orchestrator.md`** (minimal IDs only) vs **`core-pack.scene.md`** for pinned scene specialists, or generate core pack lines from `context_selector.always_include_in_core_pack` **filtered by specialist**.
- **Authoring rule:** long examples and “how to chain tools” narratives belong in **`prompts/chunks/specialists/...`**, not in `tools.main.json` summaries — JSON stays **short, precise, schema-focused**.

### 6.7 Context selection and transcript shaping

Two different systems must stay aligned:

1. **Tool surface (what tools appear in the LLM request)** — `UnrealAiToolSurfacePipeline` (BM25, domain bias, dynamic K, `tool_surface.domain_tags`, session prior). **Orchestrator** turns (`FUnrealAiAgentTurnRequest::IsOrchestratorAgentToolSurface()` + default `bOmitMainAgentBlueprintMutationTools`) use a **two-tool allow-list** appendix (`orchestrator_allow_list` telemetry) matching `UnrealAiOrchestratorToolPolicy`. **Product specialist** turns (`ActiveProductSpecialistId != None`) use **`allow_list_only`**: full roster = tools passing `UnrealAiProductSpecialistToolPolicy`, stable order (**pinned core** via `UnrealAiProductSpecialistCoreTools`, then remaining ids sorted); telemetry `product_specialist_allow_list`. Escape hatch: `bOmitMainAgentBlueprintMutationTools == false` keeps BM25 tiering on the default Agent path. Optional later: tiny BM25 tie-break *within* specialist lists only.
2. **Context window (what project/thread state is injected)** — `FUnrealAiContextService::BuildContextWindow` and placeholders such as `{{CONTEXT_SERVICE_OUTPUT}}` in chunks (`prompts/README.md`).

**Delegation-specific context:**

- **Shipped (v1):** orchestrator inner text from **`<unreal_ai_delegate>`** is copied into **`{{SPECIALIST_DELEGATION_BRIEF}}`** (see `FUnrealAiAgentTurnRequest::LastProductSpecialistDelegationBrief` → prompt assembly). Optional later: also mirror key fields into **`{{CONTEXT_SERVICE_OUTPUT}}`** for redundancy or logging.
- Longer-term: a structured block (YAML/JSON) inside context for goal, paths, GUIDs, stop conditions — or reuse **`{{CONTEXT_SERVICE_OUTPUT}}`** only on specialist turns. Keeps the specialist narrative prompt short while **facts** live in context assembly.
- **Thread state** — ensure builder/specialist sub-turns record **compact** summaries back to the parent thread (existing pattern for blueprint/environment results) so the orchestrator does not re-ingest full tool traces unless needed.

**Catalog linkage:**

- Extend **`context_selector`** with **`specialist_core`** booleans (or a `specialist_ids[]` list) mirroring how `blueprint_builder_core` boosts builder tools in tiered ranking (`UnrealAiToolSurfacePipeline.cpp`). Orchestrator stack uses a **different** core set than scene/asset specialists.

### 6.8 CI and reviews

- **Validate** merged catalog + manifest references (every path in `prompts/assemblies/*.json` exists).
- **Lint** prompt chunks: max line count or max file size warning in a small script.
- **Diff discipline** — specialist shards mean PRs touch **one domain** most of the time.

---

## 7. Implementation steps (phased)

### Phase 0 — Inventory & policy

1. Freeze a **taxonomy table**: specialist ID → allowed `tool_id` list (spreadsheet or generated from catalog).
2. **Cut the old default** — remove or rewrite code paths that expose the full catalog (or broad `main_agent` mutations) on the user-facing turn; the only entry shape is **orchestrator → specialists**.
3. Document **handoff XML/JSON blocks** in `prompts/chunks/common/` (and specialist trees per §6); replace ad hoc prose with the **single delegation contract**.

### Phase 1 — Catalog / metadata

1. Extend **`agent_surfaces`** (or add `specialist_surfaces` / `delegation_profiles` in `meta`) so each tool declares which specialists may invoke it.
2. Add **`context_selector.specialist_core`** (or equivalent) for **every** specialist profile so tiering/core-pack logic is explicit — no “optional later” for the rebuilt product.
3. Introduce **`tools.<domain>.json` fragments** for new tools so `tools.main.json` does not grow without bound; update `meta.tool_catalog_fragments`.
4. Add **`prompts/assemblies/*.json`** (or equivalent) and migrate **`FUnrealAiLinearPromptAssemblyStrategy`** toward manifest-driven chunk lists; add **`prompts/chunks/specialists/<id>/`** trees per §6.
5. Generate **`prompts/tools/by-specialist/`** (optional) and **core-pack** slices from catalog metadata.

### Phase 2 — Harness & request builder

1. Add parsing for **new handoff tags** (parallel to `UnrealAiBuildBlueprintTag`): e.g. `<unreal_ai_delegate specialist="asset_librarian">...</unreal_ai_delegate>`.
2. In `FUnrealAiAgentHarness`, implement **sub-turn scheduling** (prefer a dedicated scheduler helper per §6.3): orchestrator round → specialist round(s) → optional verifier round → resume orchestrator with results.
3. In `UnrealAiTurnLlmRequestBuilder` / tool surface pipeline:
   - For specialist runs, **disable dynamic K** or clamp to **allow-list only**.
   - Pass **specialist system prompt stack** (from manifest) + **trimmed tool index**.
4. In **`FUnrealAiContextService`**, inject **delegation brief** / structured handoff fields for specialist turns (§6.7).
5. Wire **telemetry** (existing enforcement events pattern) for `specialist_id`, rounds, and tool_finish rates.

### Phase 3 — Blueprint & environment refinements

1. **Pin tool lists per `EUnrealAiBlueprintBuilderTargetKind`** so `material_graph_*` never appears on K2-only turns and vice versa.
2. ~~Split Environment builder profiles~~ — removed from product (see README **Scope**).

### Phase 4 — Micro-agents (product scope)

1. Implement **scout → mutator** two-hop for `blueprint_graph_patch` and `material_graph_patch` where graph risk warrants it (policy in harness, not a legacy toggle).
2. Add **graph patch planner** round when the planner/executor split is in scope for the release train.

### Phase 5 — QA

1. Extend qualitative suites under `tests/qualitative-tests/suites/` with **delegation** and **specialist isolation** cases.
2. Measure: tool selection error rate, wrong-surface invocations, average appendix size per turn, tokens per user goal.

---

## 8. Success criteria

1. **Smaller effective tool sets** per LLM call (measured: median count of tool lines in appendix).
2. **Higher operational success rate** on graph patch + asset workflows (harness JSONL / manual triage).
3. **Catalog growth** — new tools added to **specialist slices** without increasing main-agent appendix size.
4. **Repeatability** — same scripted request surfaces the **same tool IDs** for specialist turns (allow-list mode).

---

## 9. Open questions

1. Do specialists share a **single model profile** or cheaper routing model for L0/L1 router?
2. How to version **specialist allow-lists** independently of the global catalog version (`meta.catalog_version`)?
3. What is the **minimum** scout/mutator/verifier ladder that ships in v1 of the rebuild vs deferred to Phase 4?

---

*Document version: 1.6 — 2026-04-14*

*1.6: Allow-list tool appendix rebuilt on continuation LLM rounds (orchestrator + specialists); BM25 tiering still round-1 only; orchestrator ids centralized in `UnrealAiOrchestratorToolPolicy`.*

*1.5: Orchestrator tool appendix — same pipeline `allow_list` path for snapshot + log tools (`orchestrator_allow_list` telemetry); `IsOrchestratorAgentToolSurface()` on turn request.*

*1.4: Specialist tool surface — `allow_list_only` path in `UnrealAiToolSurfacePipeline` (pinned core + sorted remainder); telemetry `product_specialist_allow_list`.*

*1.3: Animation / sequencer specialist split from asset librarian; aligns with `specialist="animation"` and `animation_sequencer` tool policy.*

*1.2: Rebuild posture — removed dual-path / backward-compatibility defaults; orchestrator-only tool policy; phased flags reframed.*
