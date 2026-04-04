# PCG / environment “subagent” (Nwiro-style) — feasibility and implementation plan

This document answers whether we can add a **second dedicated sub-turn pipeline** (like **Blueprint Builder**) aimed at **scenes, terrain, foliage, and PCG**, inspired by workflows described for **Nwiro** (AI-assisted environment generation in Unreal). It lists **exact implementation steps** aligned with shipped architecture in `Plugins/UnrealAiEditor`.

**Related:** [`subagents-architecture.md`](subagents-architecture.md) (definition of “subagent” as a delegated harness run), [`../tooling/tool-dispatch-inventory.md`](../tooling/tool-dispatch-inventory.md), `Plugins/UnrealAiEditor/prompts/README.md` (chunk assembly map).

---

## 1. Reference material status

- The repo’s reliability note mentions **`reference/nwiro`** and `nwiro-docs.txt` as **PCG / environment oriented** ([`BLUEPRINT_GRAPH_PATCH_RELIABILITY.md`](../../BLUEPRINT_GRAPH_PATCH_RELIABILITY.md)), but **`reference/nwiro` is not present in this workspace** (nothing under `reference/` is checked in here). For detailed parity with Nwiro’s UX, **vendor docs or a local clone should be synced** into `reference/nwiro` (or equivalent) so chunk authors can cite concrete flows.
- **Public product framing** (for expectations, not as a spec): Nwiro-style tools emphasize **natural-language scene intent**, **use of project meshes**, **layered vertical composition** (canopy / mid / ground), **density control**, and **iterative chat refinement** before committing placement ([nwiro.ai](https://nwiro.ai/), marketplace/partner pages). Our plugin would **not** copy proprietary behavior; we would **expose Unreal-native primitives** (PCG, Landscape, Foliage, actors) behind the same **handoff + filtered tool surface** pattern we already use for graphs.

---

## 2. Is this feasible?

**Yes, architecturally.** The codebase already implements the full pattern once:

| Concern | Blueprint Builder precedent |
|--------|-------------------------------|
| Sub-turn flag on the request | `FUnrealAiAgentTurnRequest::bBlueprintBuilderTurn`, `BlueprintBuilderTargetKind` |
| Alternate system prompt stack | `FUnrealAiLinearPromptAssemblyStrategy` when `Params.bBlueprintBuilderMode` ([`UnrealAiPromptAssemblyStrategy.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Prompt/UnrealAiPromptAssemblyStrategy.cpp)) |
| Domain-specific markdown | `prompts/chunks/blueprint-builder/**` + `kinds/*.md` |
| Tool gating | `agent_surfaces` in merged catalog + `EUnrealAiToolSurfaceKind` + [`UnrealAiAgentToolGate.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiAgentToolGate.cpp) + [`UnrealAiToolSurfacePipeline.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolSurfacePipeline.cpp) |
| Handoff from main agent | `<unreal_ai_build_blueprint>` parsing in harness ([`FUnrealAiAgentHarness.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/FUnrealAiAgentHarness.cpp)), tag helpers [`UnrealAiBuildBlueprintTag.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiBuildBlueprintTag.cpp) |
| Main-agent delegation prose | [`12-build-blueprint-delegation.md`](../../Plugins/UnrealAiEditor/prompts/chunks/12-build-blueprint-delegation.md) |

An **Environment / PCG Builder** would be a **parallel stack**: new request fields, new assembly branch, new chunk directory, new `agent_surfaces` token, new handoff tag, and (optionally) a small **tool-surface module** mirroring `UnrealAiBlueprintBuilderToolSurface`.

**Caveat (product depth):** The catalog already lists **`landscape_foliage_pcg`** tools such as `pcg_generate`, `foliage_paint_instances`, and `landscape_import_heightmap`, but as of this writing they are marked **`status: "future"`** in `UnrealAiToolCatalog.json`. So:

- **Prompt + harness + surface work** can ship **before** every PCG tool is implemented, but **Nwiro-like outcomes** require **implementing or stubbing** those dispatches and tightening schemas.
- Until then, the subagent can still orchestrate **implemented** world tools (e.g. actor spawn/transform/attach, level/scene discovery—whatever the catalog marks `implemented`) and **read-only** asset/scene context; prompts must **not** promise PCG execution that the dispatcher cannot perform.

---

## 3. Target behavior (what “like Nwiro” means in *our* stack)

Translate product ideas into **Unreal-editor facts** the model can drive:

1. **Intent → plan → execute** — Deterministic loop in dedicated chunks (mirror `blueprint-builder/01-deterministic-loop.md`): discover current level, relevant PCG actors/components, landscape actors, foliage types from the project, then mutate, then verify (generate, bounds check, optional PIE smoke).
2. **Project-grounded assets** — Force discovery of **real** `/Game/...` paths (static meshes, foliage types, PCG graphs) via search/index tools; forbid invented asset names in handoffs.
3. **Layering** — Prompts describe **vertical bands** and **logical layers** (ground cover vs trees vs structures) as **separate PCG passes**, **separate PCG components**, or **ordered tool calls**, not as magic.
4. **Main agent stays thin** — Main Agent does **not** call environment-mutation tools that we reserve for the builder surface (same idea as blueprint mutators withheld on main agent); it emits a **single structured handoff block** with YAML frontmatter (`target_kind` analog).

**Not required for v1:** spline-based “Place” UX like some Nwiro descriptions—only if we add tools for spline actors / PCG spline components and document them in chunks.

---

## 4. Design sketch (components to add)

### 4.1 Naming (suggested)

- Handoff tag: **`<unreal_ai_build_environment>`** (symmetric with `<unreal_ai_build_blueprint>`).
- Result tag (builder → parent): **`<unreal_ai_environment_builder_result>`** (mirror `<unreal_ai_blueprint_builder_result>` pattern in harness).
- `agent_surfaces` token: **`environment_builder`** (new string alongside `main_agent`, `blueprint_builder`).
- Chunk folder: **`prompts/chunks/environment-builder/`** with optional **`prompts/chunks/environment-builder/kinds/*.md`** selected by frontmatter.

### 4.2 Request / assembly parameters

Extend `FUnrealAiAgentTurnRequest` and `FUnrealAiPromptAssembleParams` with:

- `bool bEnvironmentBuilderTurn` (default false; **mutually exclusive** with `bBlueprintBuilderTurn` except where product explicitly allows stacking—recommend **hard mutual exclusion** in validation).
- `EUnrealAiEnvironmentBuilderTargetKind` (enum), e.g. `pcg_scene`, `landscape_terrain`, `foliage_scatter`, `mixed`—analogous to `EUnrealAiBlueprintBuilderTargetKind`.
- `bool bInjectEnvironmentBuilderResumeChunk` (optional; mirror `bInjectBlueprintBuilderResumeChunk` so parent turn gets a short “what the builder did” fragment).

`UnrealAiTurnLlmRequestBuilder` must map request → `FUnrealAiPromptAssembleParams` exactly as it does for Blueprint Builder (`bBlueprintBuilderMode` ↔ `bBlueprintBuilderTurn`).

### 4.3 Prompt assembly

In `FUnrealAiLinearPromptAssemblyStrategy::BuildSystemDeveloperContent`:

- If `bEnvironmentBuilderMode`: load **`01-identity`**, mode slice from **`02-operating-modes`**, then **fixed-ordered** `environment-builder/00-*.md` … `environment-builder/N-*.md`, then **kind** chunk from enum → relative path, then **`05-context-and-editor`**, **`07-safety-banned`**, **`08-output-style`**, then `ApplyTemplateTokens`.
- **Do not** load Blueprint Builder graph/T3d chunks unless a kind explicitly needs cross-over (prefer **no crossover** in v1).

Update **`Plugins/UnrealAiEditor/prompts/README.md`** composition matrix and canonical assembly table (same obligation as today when changing C++ order).

### 4.4 Tool surface

1. Add **`EUnrealAiToolSurfaceKind::EnvironmentBuilder`** in [`UnrealAiToolSurfaceCompatibility.h`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolSurfaceCompatibility.h) and handle it in `ToolAllowedOnSurface` with token `environment_builder`.
2. Add `GAgentSurfaceToken_EnvironmentBuilder` constant.
3. Extend or replace with [`UnrealAiAgentToolGate.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiAgentToolGate.cpp) so the active surface is **Main** vs **BlueprintBuilder** vs **EnvironmentBuilder** from request flags (shipped).
4. In [`UnrealAiToolSurfacePipeline.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolSurfacePipeline.cpp), add a branch similar to `bBlueprintBuilderTurn`:
   - Optional **hybrid retrieval** query augmentation for environment keywords.
   - **Merge “core” environment tools** after guardrails (mirror `MergeBlueprintCoreToolsAfterGuardrails`).
5. **Catalog:** For each tool that should only appear on the environment sub-turn, set `"agent_surfaces": ["environment_builder"]` (and add `main_agent` only for read-only tools shared with main). For tools that both surfaces need, list both tokens explicitly (same pattern as some material tools listing `main_agent` + `blueprint_builder`).

### 4.5 Harness handoff and completion

In `FUnrealAiAgentHarness` (where Blueprint Builder sub-turn is started and consumed):

- Parse `<unreal_ai_build_environment>` blocks: strip from visible assistant text, set `bEnvironmentBuilderTurn = true`, set `EnvironmentBuilderTargetKind` from YAML, prepend internal user message (mirror `BuildAutomatedSubturnHarnessPreamble`—add `UnrealAiEnvironmentBuilderToolSurface::BuildAutomatedSubturnHarnessPreamble` or shared helper).
- On completion, consume result tag, flip flags off, optionally set resume chunk injection.
- **Plan threads:** Reuse the same policy as Blueprint Builder: **do not** start environment builder sub-turns from `*_plan_*` threads unless product explicitly extends that; document in [`subagents-architecture.md`](subagents-architecture.md) or this doc.

### 4.6 Main-agent delegation chunk

Add **`prompts/chunks/13-build-environment-delegation.md`** (or next free number) and insert it in the **main** stack (not inside environment-builder folder) so the main agent knows:

- Which tools are **withheld** on main agent when gating is on.
- Exact handoff tag and **allowed `target_kind`** values.
- Discovery-first rules for PCG actors, landscapes, and foliage asset paths.

Wire loading in `FUnrealAiLinearPromptAssemblyStrategy` in the same tier as `12-build-blueprint-delegation.md` (always loaded for Agent/Ask/Plan with prose scoped by mode).

---

## 5. Chunk outline (`prompts/chunks/environment-builder/`)

Suggested files (titles adjustable; **order fixed in C++**):

| File | Purpose |
|------|--------|
| `00-overview.md` | Role: you are the environment subagent; scope; output contract. |
| `01-deterministic-loop.md` | Discover → act → verify; when to stop; max tool rounds discipline. |
| `02-unreal-pcg-model.md` | PCG Component, graph assets, volume vs grid, execution in editor, common failure modes. |
| `03-landscape-and-height.md` | Landscape actors, layers, heightmap import caveats, scale. |
| `04-foliage-and-instances.md` | Foliage types, painting vs procedural, performance warnings. |
| `05-scene-safety.md` | Large edits, undo expectations, destructive ops, level save prompts. |
| `06-verification-ladder.md` | Ordered checks: PCG generate success → bounds/overlap sanity → optional PIE. |
| `kinds/pcg_scene.md` | Deep rules when `target_kind: pcg_scene`. |
| `kinds/landscape_terrain.md` | Deep rules for terrain-heavy tasks. |
| `kinds/foliage_scatter.md` | Deep rules for foliage-first tasks. |
| `kinds/mixed.md` | Ordering when multiple subsystems touch the same region. |

Authors should refresh chunks when **`UnrealAiToolCatalog.json`** changes (new parameters, implemented status).

---

## 6. Implementation steps (ordered)

### Phase A — Inventory and reference sync

1. **Add or restore `reference/nwiro`** (docs only) under repo policy so prompt authors can cite **workflows**, not code—we do not depend on Nwiro’s implementation.
2. **Audit** all `landscape_foliage_pcg` (and related scene) tools in `UnrealAiToolCatalog.json`: `status`, parameters, and actual dispatch coverage in `UnrealAiToolDispatch*.cpp`.
3. **Decide MVP surface**: minimum set of **implemented** tools the environment builder may call on day one; mark others as “prompt must not rely” until Phase C.

### Phase B — Harness + prompts + surface (vertical slice)

4. Add enum `EUnrealAiEnvironmentBuilderTargetKind` + `TargetKind.cpp` helpers (`ParseFromString`, `KindChunkFileName`) mirroring [`UnrealAiBlueprintBuilderTargetKind.*`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Public/UnrealAiBlueprintBuilderTargetKind.h).
5. Extend `FUnrealAiAgentTurnRequest` / `FUnrealAiPromptAssembleParams` with environment-builder fields; **validate** mutual exclusion with blueprint builder.
6. Implement tag parse/strip/result consume (**new** `UnrealAiBuildEnvironmentTag.cpp` or generalize existing tag module with shared YAML frontmatter parser if duplication is too high).
7. **Prompt assembly branch** in `UnrealAiPromptAssemblyStrategy.cpp` + create **`environment-builder/*.md`** skeleton files with accurate “do not promise unimplemented tools” language from Phase A.
8. **Tool surface token** `environment_builder` + `EUnrealAiToolSurfaceKind::EnvironmentBuilder` + gate/pipeline wiring.
9. **Catalog** updates: tag environment-mutation tools with `agent_surfaces` appropriate to withholding strategy; ensure main agent chunk lists withheld ids.
10. **Main delegation chunk** + insert into linear strategy after blueprint delegation (or adjacent—match product preference).
11. **Tests**: extend [`UnrealAiToolDispatchAutomationTests.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatchAutomationTests.cpp) for surface token sets; add prompt assembly tests if present for blueprint builder; run `./build-editor.ps1 -Headless` per workspace rules.

### Phase C — Tooling depth (Nwiro-like quality)

12. **Implement `pcg_generate`** dispatch (editor-world PCG execution, clear errors for missing component, graph compile failures, bounds).
13. **Implement `foliage_paint_instances`** and **`landscape_import_heightmap`** (or narrow schemas to what we can support reliably).
14. **Optional composite tools** (strongly recommended for model success rate), following the lesson in [`BLUEPRINT_GRAPH_PATCH_RELIABILITY.md`](../../BLUEPRINT_GRAPH_PATCH_RELIABILITY.md):
    - e.g. `pcg_graph_summarize` (read-only), `pcg_list_graph_parameters`, `environment_query_bounds`—**closed vocabulary** beats raw memory of editor objects.
15. **Optional “mesh shortlist” support**: if Nwiro-style indexing is desired, add **read** tools that return **candidate static meshes / foliage types** with stable paths (could reuse/adjacent to existing asset search tools).

### Phase D — Product polish

16. **Resume chunk** and telemetry parity with blueprint builder (appendix budget, surface profile id).
17. **Documentation**: update [`docs/tooling/tool-dispatch-inventory.md`](../tooling/tool-dispatch-inventory.md) and tooling README chunks under `prompts/tools/` when catalog/surface changes stabilize.
18. Revisit **plan-node** and **parallel subagent** policy: environment work is **high contention** on the level (see [`subagents-architecture.md`](subagents-architecture.md) §2.4)—keep **serial** default and document.

---

## 7. Risks and mitigations

| Risk | Mitigation |
|------|------------|
| PCG / landscape APIs are **heavy** and editor-state sensitive | Strict verification ladder in chunks; small geographic scopes; encourage duplicate level or sandbox sublevel for experiments. |
| Catalog tools **`future`** vs prompts promising them | Phase A inventory + chunk text generated from truth table; integration tests when status flips to `implemented`. |
| Overlap with **main agent** scene tools | Clear `agent_surfaces` + delegation chunk; same escape-hatch policy as `meta.agent_surfaces.escape_hatch` if present. |
| **Mutex / world** contention with parallel plan nodes | Default **no** environment builder from plan threads; treat world edits as **serial** scope in parallel policy. |

---

## 8. Summary

- **Feasible:** Reuse the **Blueprint Builder** decomposition (handoff tag, `b*BuilderTurn`, alternate chunk stack, `agent_surfaces` token, pipeline merge, harness loop).
- **Nwiro-like UX** additionally depends on **implemented** PCG/Landscape/Foliage (or composite) tools—not only prompts.
- **First step:** Sync **`reference/nwiro`** docs for authoring alignment, then run **Phase A** audit so prompts and catalog stay honest about what the editor can execute today.
