# Unreal AI Editor — master tool registry

**Version:** 1.1  
**Target engine:** Unreal Engine 5.5+ (minor API names may drift — confirm against your installed `Engine/Source` before implementation.)  

**Canonical machine-readable catalog:** A **single JSON** ships with the editor plugin: [`Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`](../../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json) (`meta` + `tools[]`). Runtime execution: `FUnrealAiToolExecutionHost` + `UnrealAiToolDispatch.cpp` in `Private/Tools/`. Extend handlers there and keep this document in sync for narrative/Epic links.

**This Markdown file** remains the **human-readable** narrative, Epic links, and engineering notes.

**Related:** [`context-management.md`](../context/context-management.md), [`context-ingestion-refactor.md`](../planning/context-ingestion-refactor.md) (layered candidate pipeline plan), [`AGENT_HARNESS_HANDOFF.md`](AGENT_HARNESS_HANDOFF.md), [`tools-expansion.md`](tools-expansion.md) (tool surface pipeline, compiled defaults in `UnrealAiRuntimeDefaults.h`, architecture map), [`tool-catalog-audit-guide.md`](tool-catalog-audit-guide.md) (iterative catalog review using harness results), repo [`README.md`](../README.md).

---

## Runtime tool surface (wire format + optional eligibility)

The catalog is still the **single schema source**, but **how much** of it reaches the model depends on mode and compiled defaults in [`UnrealAiRuntimeDefaults.h`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Misc/UnrealAiRuntimeDefaults.h):

| Mechanism | What it does |
|-----------|----------------|
| **Native `tools[]`** | `ToolSurfaceUseDispatch = false` — full function definitions per enabled tool (large payload). |
| **Dispatch (default)** | `ToolSurfaceUseDispatch = true` — tiny `tools[]` with `unreal_ai_dispatch` + **markdown tool index** in the system/developer text (`UnrealAiTurnLlmRequestBuilder`). |
| **Tiered eligibility** | **Default on** for Agent + dispatch + **LLM round 1** (`UnrealAiToolSurfacePipeline` ranks via **BM25 + domain bias + session prior blend**, **dynamic K**, guardrail merge, **budgeted** index). Set `ToolEligibilityTelemetryEnabled = false` in the defaults header to skip the tiered path. See [`tools-expansion.md`](tools-expansion.md) §10 and [`architecture-maps/architecture.dsl`](../architecture-maps/architecture.dsl) view **`tool-surface-graph`**. |

Optional per-tool metadata: `tools[].tool_surface.domain_tags` (see `meta.tool_surface` in the catalog JSON). **Docs/project vector retrieval** (`Retrieval Service`) is unrelated; it feeds **context**, not the tool roster.

### Main Agent vs Blueprint Builder (default product path)

- **Default Agent** (`Mode == Agent` with `bOmitMainAgentBlueprintMutationTools`): the tiered tool roster is filtered by **`tools[].agent_surfaces`** (see `meta.agent_surfaces` in the catalog). Graph mutators such as **`blueprint_graph_patch`**, **`blueprint_apply_ir`**, **`blueprint_compile`**, **`blueprint_format_graph`**, and **`blueprint_add_variable`** are **`blueprint_builder`–only** unless the field is missing/empty/`["all"]`.
- **Handoff:** the main agent emits **`<unreal_ai_build_blueprint>`** with YAML **`target_kind`**; the harness runs a **Blueprint Builder** sub-turn with the builder prompt stack and domain-filtered tools (`UnrealAiBuildBlueprintTag`, `FUnrealAiAgentHarness`, `UnrealAiBlueprintBuilderToolSurface`).
- **Escape hatch:** when **`bOmitMainAgentBlueprintMutationTools`** is **false**, surface gating is bypassed so power users can expose graph tools on the main roster.

Implementation: **`UnrealAiAgentToolGate.cpp`**, **`UnrealAiToolSurfaceCompatibility.cpp`**, prompts under **`Plugins/UnrealAiEditor/prompts/chunks/`** (especially **`04`**, **`10`**, **`12`**, **`14`**, **`blueprint-builder/**`**, **`environment-builder/**`**). PCG/landscape/foliage tool defs: **`Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalogFragmentPcgEnvironment.json`** (merged at load).

---

## How this document was produced

| Phase | What we did |
|-------|----------------|
| **Breadth** | Enumerated candidates from product requirements, common editor-agent patterns; merged overlaps. |
| **Depth** | Mapped each tool to **Unreal modules/classes** and **game-thread** expectations; flagged **banned** or **future** items. |
| **Docs** | Linked **Epic Developer Documentation** API pages where stable URLs exist; engine source remains authoritative for threading and deprecations. |

**Research stance:** Prefer Epic’s official docs + local engine headers over bulk copying doc text. Optional later: index **offline** HTML help from an installed engine.

---

## Standard tool entry template

Every tool below follows this schema (suitable for export to JSON for OpenAI/Anthropic tool definitions):

| Field | Description |
|-------|-------------|
| **`tool_id`** | Stable `snake_case` identifier. |
| **`summary`** | One-line description for the model. |
| **`parameters`** | Name, type, required, constraints. |
| **`returns`** | Structured fields (paths, IDs, booleans). |
| **`side_effects`** | `none` \| `disk` \| `scene` \| `asset` \| `compile` \| `exec`. |
| **`permission`** | `read` \| `write` \| `destructive` \| `exec` — maps to Ask/Agent/Plan profiles. |
| **`ue_entry_points`** | Modules, types, functions (see tables below). |
| **`threading`** | Typically **game thread** for editor mutation. |
| **`failure_modes`** | User-visible errors. |
| **`doc_links`** | Epic doc URLs (API / guides). |
| **`status`** | Authoritative values and meanings: [`UnrealAiToolCatalog.json`](../../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json) `meta.status_legend` (`implemented`, `future`, `designed`, `deprecated`). The per-tool **status** cells in the tables below are not auto-synced—prefer the JSON for each `tool_id`. |

**Ask mode:** Only tools with `permission: read` (and optionally explicit read-only context tools).  
**Agent / Plan:** Catalog entries allowed by mode flags, then **tiered eligibility** and **`agent_surfaces`** (main Agent vs Blueprint Builder) further narrow what appears in the tool appendix—see [Main Agent vs Blueprint Builder](#main-agent-vs-blueprint-builder-default-product-path) above.

---

## Table of contents

1. [Legend & permission model](#legend--permission-model)  
2. [Tier-1 MVP tools (viewport, capture, selection, UI)](#tier-1-mvp-tools-viewport-capture-selection-ui)  
3. [Viewport & camera](#viewport--camera)  
4. [Capture & vision](#capture--vision)  
5. [Selection & framing](#selection--framing)  
6. [Editor UI navigation](#editor-ui-navigation)  
7. [World, levels & actors](#world-levels--actors)  
8. [Assets & content browser](#assets--content-browser)  
9. [Blueprints & graph tooling](#blueprints--graph-tooling)  
10. [Materials & rendering](#materials--rendering)  
11. [Animation, Sequencer & cinematics](#animation-sequencer--cinematics)  
12. [Landscape, foliage & PCG (staged)](#landscape-foliage--pcg-staged)  
13. [Physics & collision (staged)](#physics--collision-staged)  
14. [Audio & MetaSounds (staged)](#audio--metasounds-staged)  
15. [PIE & play sessions](#pie--play-sessions)  
16. [Build, cook & packaging](#build-cook--packaging)  
17. [Diagnostics, logs & audit](#diagnostics-logs--audit)  
18. [Project files & search](#project-files--search)  
19. [Console & gated execution](#console--gated-execution)  
20. [Banned or out-of-scope for v1](#banned-or-out-of-scope-for-v1)  
21. [Appendix: subsystems & modules to study](#appendix-subsystems--modules-to-study)  

---

## Legend & permission model

| Tag | Meaning |
|-----|---------|
| **`read`** | No mutation of project/world; safe for Ask mode. |
| **`write`** | Mutates editor state, assets, or scene; needs Agent/Plan + often user confirm. |
| **`destructive`** | Delete, rename with refs, bulk ops — strong confirmation. |
| **`exec`** | Runs commands, cooks, or OS-level actions — strict allow-list. |

---

## Tier-1 MVP tools (viewport, capture, selection, UI)

These are the first implementation wave. Each row is expanded in its domain section.

| `tool_id` | Summary | Permission | Key UE surfaces |
|-----------|---------|------------|-----------------|
| `viewport_capture_png` | Save active editor viewport to PNG under `Saved/Screenshots/AI/`. | `write` (disk) | `FEditorViewportClient`, `FScreenshotRequest`, `FViewport::ReadPixels` |
| `viewport_camera_orbit` | Orbit editor camera around pivot with delta pitch/yaw. | `write` | `FEditorViewportClient`, `FViewportCameraTransform` |
| `viewport_camera_pan` | Pan editor camera. | `write` | Same as above |
| `viewport_camera_dolly` | Dolly / zoom editor view. | `write` | Same + optional FOV |
| `viewport_frame_actors` | Frame one or more actors in active viewport (bounds + FOV fit). | `write` | `GEditor`, `FEditorViewportClient`, framing helpers |
| `editor_get_selection` | Return selected actors/assets. | `read` | `USelection`, `GEditor` |
| `editor_set_selection` | Select actors by path or reference. | `write` | `GEditor`, selection APIs |
| `asset_open_editor` | Open asset in correct editor tab. | `write` | `UAssetEditorSubsystem`, `GEditor` |
| `content_browser_sync_asset` | Focus Content Browser on asset/folder. | `write` | `FContentBrowserModule`, synchronization APIs |

**Threading:** All Tier-1 mutations **must run on the game thread** (editor tick), same as other Slate/editor commands.

**PIE vs editor:** Default tools target the **level editor viewport**, not PIE, unless a parameter explicitly requests PIE capture (future).

---

## Viewport & camera

### `viewport_camera_orbit`

| Field | Value |
|-------|--------|
| **summary** | Orbit the active level editor camera around a pivot (world location or selected actor). |
| **parameters** | `pivot` (world vector or `use_selection_center`), `yaw_deg`, `pitch_deg`, `radius` (optional), `relative` (bool). |
| **returns** | `success`, `camera_location`, `camera_rotation`. |
| **side_effects** | scene (viewport only) |
| **permission** | `write` |
| **ue_entry_points** | `FEditorViewportClient` — `GetViewportTransform` / `SetViewLocation` / `SetViewRotation`; `FLevelEditorModule::GetFirstActiveViewport`; orbit math in world space. Module: `UnrealEd`, `LevelEditor`. |
| **threading** | Game thread. |
| **failure_modes** | No active viewport; invalid pivot; pilot mode conflict. |
| **doc_links** | [FEditorViewportClient](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FEditorViewportClient), [Level Editor](https://dev.epicgames.com/documentation/en-us/unreal-engine/level-editor-in-unreal-engine) |
| **status** | `research` |

### `viewport_camera_pan`

| Field | Value |
|-------|--------|
| **summary** | Pan the active editor camera (screen- or world-space delta). |
| **parameters** | `delta` (vector), `space` (`world` \| `camera`). |
| **returns** | `success`, new transform. |
| **side_effects** | scene (viewport) |
| **permission** | `write` |
| **ue_entry_points** | `FEditorViewportClient` translation along right/up vectors; `FViewportCameraTransform`. |
| **threading** | Game thread. |
| **failure_modes** | No viewport. |
| **doc_links** | [FEditorViewportClient](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FEditorViewportClient) |
| **status** | `research` |

### `viewport_camera_dolly`

| Field | Value |
|-------|--------|
| **summary** | Move camera toward/away from look-at target or adjust FOV for zoom effect. |
| **parameters** | `dolly_units` or `fov_delta`, `mode` (`dolly` \| `fov`). |
| **returns** | `success`, transform or FOV. |
| **side_effects** | scene |
| **permission** | `write` |
| **ue_entry_points** | `FEditorViewportClient::ViewFOV` / movement along forward; clamp FOV. |
| **threading** | Game thread. |
| **failure_modes** | FOV out of range. |
| **doc_links** | [FEditorViewportClient](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FEditorViewportClient) |
| **status** | `research` |

### `viewport_camera_set_transform`

| Field | Value |
|-------|--------|
| **summary** | Set editor camera location and rotation explicitly (optional ease — future). |
| **parameters** | `location`, `rotation`, `b_snap` (bool). |
| **returns** | `success`. |
| **side_effects** | scene |
| **permission** | `write` |
| **ue_entry_points** | `FEditorViewportClient::SetViewLocation`, `SetViewRotation`; consider `MoveViewportCamera` patterns in editor code. |
| **threading** | Game thread. |
| **failure_modes** | No viewport. |
| **doc_links** | [FEditorViewportClient](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FEditorViewportClient) |
| **status** | `research` |

### `viewport_camera_get_transform`

| Field | Value |
|-------|--------|
| **summary** | Read active level editor camera location/rotation/FOV. |
| **parameters** | Optional `viewport_index`. |
| **returns** | `location`, `rotation`, `fov`, `viewport_size`. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `FEditorViewportClient::GetViewLocation`, `GetViewRotation`, `ViewFOV`. |
| **threading** | Game thread. |
| **failure_modes** | No active viewport. |
| **doc_links** | [FEditorViewportClient](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FEditorViewportClient) |
| **status** | `research` |

### `viewport_camera_pilot`

| Field | Value |
|-------|--------|
| **summary** | Pilot a `CameraActor` or exit pilot mode (ties camera to actor). |
| **parameters** | `actor_path` or `unpilot` (bool). |
| **returns** | `success`, `piloting` state. |
| **side_effects** | scene |
| **permission** | `write` |
| **ue_entry_points** | Editor pilot APIs on `FEditorViewportClient` / `ACameraActor` — **verify symbol names in your UE version** (search engine for `Pilot` in `UnrealEd`). |
| **threading** | Game thread. |
| **failure_modes** | Actor not a camera; not in world. |
| **doc_links** | [CameraActor](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/ACameraActor) |
| **status** | `research` |

### `viewport_set_view_mode`

| Field | Value |
|-------|--------|
| **summary** | Switch editor viewport rendering mode (Lit, Unlit, Wireframe, etc.). |
| **parameters** | `view_mode` (enum string). |
| **returns** | `success`. |
| **side_effects** | scene |
| **permission** | `write` |
| **ue_entry_points** | `FEditorViewportClient::SetViewMode` / `ViewMode` APIs — confirm in `UnrealEd`. |
| **threading** | Game thread. |
| **failure_modes** | Invalid mode. |
| **doc_links** | [Viewport types](https://dev.epicgames.com/documentation/en-us/unreal-engine/viewports-in-unreal-engine) |
| **status** | `research` |

---

## Capture & vision

### `viewport_capture_png`

| Field | Value |
|-------|--------|
| **summary** | Capture the active editor viewport to a PNG on disk for vision models or QA. |
| **parameters** | `filename_slug`, `resolution_scale` (float), `include_ui` (bool), `delay_frames` (int). |
| **returns** | `absolute_path`, `width`, `height`, `success`. |
| **side_effects** | disk |
| **permission** | `write` |
| **ue_entry_points** | `FScreenshotRequest::RequestScreenshot` ([API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FScreenshotRequest)); `FViewport::ReadPixels` / `FEditorViewportClient` readback; write under `Saved/Screenshots/AI/`. |
| **threading** | Request on game thread; readback may need flush **RenderThread** coordination — follow patterns in engine screenshot code. |
| **failure_modes** | No viewport; readback failed; disk full. |
| **doc_links** | [FScreenshotRequest](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FScreenshotRequest), [High Resolution Screenshots](https://dev.epicgames.com/documentation/en-us/unreal-engine/high-resolution-screenshots) |
| **status** | `research` |

### `viewport_capture_delayed`

| Field | Value |
|-------|--------|
| **summary** | Schedule capture after N frames (GPU/Temporal stability). |
| **parameters** | Same as `viewport_capture_png` + `delay_frames`. |
| **returns** | Same as capture. |
| **side_effects** | disk |
| **permission** | `write` |
| **ue_entry_points** | `FTSTicker` or frame counter + `FScreenshotRequest`; ensure flush. |
| **threading** | Game thread schedule; deferred completion. |
| **failure_modes** | Editor closed before capture. |
| **doc_links** | [FScreenshotRequest](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FScreenshotRequest) |
| **status** | `research` |

### `render_target_readback_editor`

| Field | Value |
|-------|--------|
| **summary** | Capture a named `UTextureRenderTarget2D` or scene target for advanced vision workflows. |
| **parameters** | `asset_path`, `export_path`. |
| **returns** | `success`, `path`. |
| **side_effects** | disk |
| **permission** | `write` |
| **ue_entry_points** | `FTexture2DDynamic`, `RenderTarget` readback — heavier; **staged post-MVP** for many projects. |
| **threading** | Render-thread considerations. |
| **failure_modes** | Asset not loaded; GPU readback stall. |
| **doc_links** | [TextureRenderTarget2D](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UTextureRenderTarget2D) |
| **status** | `future` |

---

## Selection & framing

### `editor_get_selection`

| Field | Value |
|-------|--------|
| **summary** | List currently selected actors (and optionally BSP/asset selection). |
| **parameters** | `include_non_actor` (bool). |
| **returns** | `actor_paths[]`, `labels[]`. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `GEditor->GetSelectedActors()`, `USelection`, iterate `AActor`. Module: `UnrealEd`. |
| **threading** | Game thread. |
| **failure_modes** | None (empty selection ok). |
| **doc_links** | [UEditorEngine](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/UEditorEngine) |
| **status** | `research` |

### `editor_set_selection`

| Field | Value |
|-------|--------|
| **summary** | Replace editor selection with specified actors. |
| **parameters** | `actor_paths[]`, `additive` (bool). |
| **returns** | `success`, `selected_count`. |
| **side_effects** | scene (selection) |
| **permission** | `write` |
| **ue_entry_points** | `GEditor->SelectActor`, `SelectNone`, `USelection`. |
| **threading** | Game thread. |
| **failure_modes** | Actor not found; wrong world. |
| **doc_links** | [UEditorEngine](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/UEditorEngine) |
| **status** | `research` |

### `viewport_frame_selection`

| Field | Value |
|-------|--------|
| **summary** | Run editor “frame selection” on current selection (same as user hotkey behavior). |
| **parameters** | `margin_scale` (float, default 1.15). |
| **returns** | `success`. |
| **side_effects** | scene |
| **permission** | `write` |
| **ue_entry_points** | Delegate to built-in **Focus Selected** / viewport framing commands — search `UnrealEd` for `FocusViewport`, `MoveViewportCamerasToActor`, `Frame` (exact names version-dependent). |
| **threading** | Game thread. |
| **failure_modes** | Empty selection; invalid bounds. |
| **doc_links** | [Viewport interaction](https://dev.epicgames.com/documentation/en-us/unreal-engine/viewport-controls-in-unreal-engine) |
| **status** | `research` |

### `viewport_frame_actors`

| Field | Value |
|-------|--------|
| **summary** | Frame a set of actors by path (PRD §6.4 — bounds + FOV fit). |
| **parameters** | `actor_paths[]`, `margin_scale`, `pitch_bias`, `yaw_bias`, `orthographic` (bool). |
| **returns** | `success`, `used_bounds`. |
| **side_effects** | scene |
| **permission** | `write` |
| **ue_entry_points** | Aggregate `AActor::GetComponentsBoundingBox` (include non-colliding as needed); compute distance from FOV + aspect; set `FEditorViewportClient` view — or wrap existing editor framing if exposed. |
| **threading** | Game thread. |
| **failure_modes** | Infinite bounds; actor not loaded. |
| **doc_links** | [AActor](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/AActor) |
| **status** | `research` |

---

## Editor UI navigation

### `asset_open_editor`

| Field | Value |
|-------|--------|
| **summary** | Open a `UObject` asset in its native editor (Blueprint, Material, Level, etc.). |
| **parameters** | `soft_object_path` or `/Game/...` string. |
| **returns** | `success`, `editor_name`. |
| **side_effects** | UI |
| **permission** | `write` |
| **ue_entry_points** | `GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset`; `FAssetEditorManager`. Modules: `UnrealEd`, `AssetTools`. |
| **threading** | Game thread. |
| **failure_modes** | Asset not found; unsupported type. |
| **doc_links** | [UAssetEditorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/UAssetEditorSubsystem) |
| **status** | `research` |

### `content_browser_sync_asset`

| Field | Value |
|-------|--------|
| **summary** | Focus Content Browser on an asset or folder (PRD §6.3). |
| **parameters** | `path` (asset or folder). |
| **returns** | `success`. |
| **side_effects** | UI |
| **permission** | `write` |
| **ue_entry_points** | `FContentBrowserModule::Get().SyncBrowserToAssets` / folder navigation APIs. Module: `ContentBrowser`. |
| **threading** | Game thread. |
| **failure_modes** | Path invalid. |
| **doc_links** | [Content Browser](https://dev.epicgames.com/documentation/en-us/unreal-engine/content-browser-in-unreal-engine) |
| **status** | `research` |

### `content_browser_navigate_folder`

| Field | Value |
|-------|--------|
| **summary** | Navigate Content Browser to a `/Game/...` folder without selecting an asset. |
| **parameters** | `folder_path`. |
| **returns** | `success`. |
| **side_effects** | UI |
| **permission** | `write` |
| **ue_entry_points** | `FContentBrowserModule`, path utilities. |
| **threading** | Game thread. |
| **failure_modes** | Folder does not exist. |
| **doc_links** | [Content Browser](https://dev.epicgames.com/documentation/en-us/unreal-engine/content-browser-in-unreal-engine) |
| **status** | `research` |

### `editor_set_mode`

| Field | Value |
|-------|--------|
| **summary** | Switch editor mode (Place, Landscape, Foliage, Mesh Paint, etc.) when allowed. |
| **parameters** | `mode_id` (allow-listed string). |
| **returns** | `success`. |
| **side_effects** | UI |
| **permission** | `write` |
| **ue_entry_points** | `GLevelEditorModeTools()` / `FEditorModeTools`; `FEdMode` registration — **strict allow-list** per PRD §6.3. |
| **threading** | Game thread. |
| **failure_modes** | Mode unavailable (plugin disabled). |
| **doc_links** | [Editor modes](https://dev.epicgames.com/documentation/en-us/unreal-engine/editor-modes-in-unreal-engine) |
| **status** | `research` |

### `global_tab_focus`

| Field | Value |
|-------|--------|
| **summary** | Bring a known dock tab to front (e.g. Level Editor, Output Log). |
| **parameters** | `tab_id` (allow-listed). |
| **returns** | `success`. |
| **side_effects** | UI |
| **permission** | `write` |
| **ue_entry_points** | `FGlobalTabmanager::Get()->TryInvokeTab(FName(...))`; map IDs from `WorkspaceMenu`. |
| **threading** | Game thread. |
| **failure_modes** | Tab not registered. |
| **doc_links** | [Programming subsystems](https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-with-cplusplus-in-unreal-engine) |
| **status** | `research` |

### `menu_command_invoke`

| Field | Value |
|-------|--------|
| **summary** | Execute a **pre-registered** editor command by name (never arbitrary strings). |
| **parameters** | `command_id` (enum / allow-list). |
| **returns** | `success`. |
| **side_effects** | varies |
| **permission** | `exec` |
| **ue_entry_points** | `FUICommandList::ExecuteAction`, `ILevelEditor` command bindings — only whitelisted IDs. |
| **threading** | Game thread. |
| **failure_modes** | Unknown command. |
| **doc_links** | [UI commands](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-ui-programming-guide) |
| **status** | `research` |

---

## World, levels & actors

### `actor_spawn_from_class`

| Field | Value |
|-------|--------|
| **summary** | Spawn an actor of a given class at a transform in the current editor world. |
| **parameters** | `class_path`, `location`, `rotation`, `folder_path` (optional). |
| **returns** | `actor_path`, `success`. |
| **side_effects** | scene |
| **permission** | `write` |
| **ue_entry_points** | `GEditor->GetEditorWorldContext().World()`, `SpawnActor`, `ULevel::Actors`; transaction `FScopedTransaction`. Module: `UnrealEd`, `Engine`. |
| **threading** | Game thread. |
| **failure_modes** | Abstract class; outside world bounds. |
| **doc_links** | [AActor](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/AActor), [Spawning](https://dev.epicgames.com/documentation/en-us/unreal-engine/spawning-and-destroying-actors-in-unreal-engine) |
| **status** | `research` |

### `actor_destroy`

| Field | Value |
|-------|--------|
| **summary** | Destroy an actor in the editor world. |
| **parameters** | `actor_path`, optional `confirm` (auto-filled in headed runs when `AutoRunDestructiveDefault` is true in `UnrealAiRuntimeDefaults.h`). |
| **returns** | `success`. |
| **side_effects** | scene |
| **permission** | `destructive` |
| **ue_entry_points** | `World->DestroyActor`, undo buffer. |
| **threading** | Game thread. |
| **failure_modes** | Not in current world; read-only level. |
| **doc_links** | [Destroying actors](https://dev.epicgames.com/documentation/en-us/unreal-engine/spawning-and-destroying-actors-in-unreal-engine) |
| **status** | `research` |

### `actor_set_transform`

| Field | Value |
|-------|--------|
| **summary** | Set actor location / rotation / scale. |
| **parameters** | `actor_path`, `transform`, `teleport_physics` (bool). |
| **returns** | `success`. |
| **side_effects** | scene |
| **permission** | `write` |
| **ue_entry_points** | `AActor::SetActorTransform`, `SetActorLocation` — with transaction. |
| **threading** | Game thread. |
| **failure_modes** | Locked actor; construction script constraints. |
| **doc_links** | [AActor](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/AActor) |
| **status** | `research` |

### `actor_get_transform`

| Field | Value |
|-------|--------|
| **summary** | Read actor transform and mobility. |
| **parameters** | `actor_path`. |
| **returns** | `transform`, `mobility`. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `AActor::GetActorTransform`. |
| **threading** | Game thread. |
| **failure_modes** | Not found. |
| **doc_links** | [AActor](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/AActor) |
| **status** | `research` |

### `actor_find_by_label`

| Field | Value |
|-------|--------|
| **summary** | Find actors by label or tag in current level. |
| **parameters** | `label`, `tag` (optional). |
| **returns** | `actor_paths[]`. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `TActorIterator`, `UGameplayStatics::GetAllActorsOfClass` (PIE/editor world context). |
| **threading** | Game thread. |
| **failure_modes** | Large levels — return capped list. |
| **doc_links** | [Actor iteration](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-and-object-references-in-unreal-engine) |
| **status** | `research` |

### `actor_attach_to`

| Field | Value |
|-------|--------|
| **summary** | Attach child actor to parent (editor). |
| **parameters** | `child_path`, `parent_path`, `socket` (optional). |
| **returns** | `success`. |
| **side_effects** | scene |
| **permission** | `write` |
| **ue_entry_points** | `AActor::AttachToActor` / `AttachToComponent`. |
| **threading** | Game thread. |
| **failure_modes** | Cycle; incompatible components. |
| **doc_links** | [Attachment](https://dev.epicgames.com/documentation/en-us/unreal-engine/attaching-actors-together-in-unreal-engine) |
| **status** | `research` |

### `actor_set_visibility`

| Field | Value |
|-------|--------|
| **summary** | Toggle actor hidden in game/editor view. |
| **parameters** | `actor_path`, `hidden`, `affect_children`. |
| **returns** | `success`. |
| **side_effects** | scene |
| **permission** | `write` |
| **ue_entry_points** | `AActor::SetActorHiddenInGame`, visibility flags on primitives. |
| **threading** | Game thread. |
| **failure_modes** | — |
| **doc_links** | [AActor](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/AActor) |
| **status** | `research` |

### `outliner_folder_move`

| Field | Value |
|-------|--------|
| **summary** | Move actors to an outliner folder (organizational). |
| **parameters** | `actor_paths[]`, `folder_name`. |
| **returns** | `success`. |
| **side_effects** | scene / serialization |
| **permission** | `write` |
| **ue_entry_points** | `FActorFolders` / world outliner APIs — verify in `Editor` modules. |
| **threading** | Game thread. |
| **failure_modes** | Folder API changes by version. |
| **doc_links** | [World Outliner](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-outliner-in-unreal-engine) |
| **status** | `research` |

---

## Assets & content browser

### `asset_registry_query`

| Field | Value |
|-------|--------|
| **summary** | Query `FAssetRegistryModule` for assets by path, class, tags. |
| **parameters** | `path_filter`, `class_name`, `max_results`. |
| **returns** | `assets[]` with `{name, path, class}`. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `FAssetRegistryModule::Get()`, `IAssetRegistry::GetAssets`, `FARFilter`. Module: `AssetRegistry`. |
| **threading** | Asset registry queries are typically game thread; async scan APIs exist for large queries. |
| **failure_modes** | Registry not fully loaded — wait for scan complete. |
| **doc_links** | [Asset Registry](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-registry-in-unreal-engine), [IAssetRegistry](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Developer/AssetRegistry/IAssetRegistry) |
| **status** | `research` |

### `asset_get_metadata`

| Field | Value |
|-------|--------|
| **summary** | Read tags, dependencies summary, and package info for an asset. |
| **parameters** | `object_path`. |
| **returns** | JSON-safe metadata blob. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `IAssetRegistry::GetAssetByObjectPath`, `GetDependencies`; `FAssetData`. |
| **threading** | Game thread. |
| **failure_modes** | Asset not indexed. |
| **doc_links** | [FAssetData](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AssetRegistry/FAssetData) |
| **status** | `research` |

### `asset_save_packages`

| Field | Value |
|-------|--------|
| **summary** | Save dirty packages (selected assets or all dirty). |
| **parameters** | `object_paths[]` or `all_dirty`. |
| **returns** | `saved[]`, `failed[]`. |
| **side_effects** | disk |
| **permission** | `write` |
| **ue_entry_points** | `UPackage::Save`, `UEditorLoadingAndSavingUtils::SavePackages`, `FEditorFileUtils`. Module: `UnrealEd`. |
| **threading** | Game thread; can be slow. |
| **failure_modes** | Checkout failure (Perforce); read-only. |
| **doc_links** | [Saving packages](https://dev.epicgames.com/documentation/en-us/unreal-engine/saving-assets-in-unreal-engine) |
| **status** | `research` |

### `asset_rename`

| Field | Value |
|-------|--------|
| **summary** | Rename/move asset and optionally fix references. |
| **parameters** | `source_path`, `dest_path`, `fix_references` (bool). |
| **returns** | `success`. |
| **side_effects** | disk, refs |
| **permission** | `destructive` |
| **ue_entry_points** | `IAssetTools::RenameAssets` / `Migrate`; `UAssetRenameData`. Module: `AssetTools`. |
| **threading** | Game thread. |
| **failure_modes** | Locked files; name collision. |
| **doc_links** | [Asset Tools](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine) |
| **status** | `research` |

### `asset_delete`

| Field | Value |
|-------|--------|
| **summary** | Delete asset files (with confirmation). |
| **parameters** | `object_paths[]`, `confirm`. |
| **returns** | `success`. |
| **side_effects** | disk |
| **permission** | `destructive` |
| **ue_entry_points** | `ObjectTools::DeleteObjects`, `IAssetTools::DeleteAssets`. |
| **threading** | Game thread. |
| **failure_modes** | In use; source controlled. |
| **doc_links** | [Deleting assets](https://dev.epicgames.com/documentation/en-us/unreal-engine/deleting-assets-in-unreal-engine) |
| **status** | `research` |

### `asset_duplicate`

| Field | Value |
|-------|--------|
| **summary** | Duplicate asset to new name/path. |
| **parameters** | `source_path`, `dest_path`. |
| **returns** | `new_path`. |
| **side_effects** | disk |
| **permission** | `write` |
| **ue_entry_points** | `IAssetTools::DuplicateAsset`, `AssetEditorUtilities`. |
| **threading** | Game thread. |
| **failure_modes** | Name collision. |
| **doc_links** | [Asset duplication](https://dev.epicgames.com/documentation/en-us/unreal-engine/duplicating-assets-in-unreal-engine) |
| **status** | `research` |

### `asset_import`

| Field | Value |
|-------|--------|
| **summary** | Import files into `/Game` via automated import pipeline. |
| **parameters** | `source_files[]`, `destination_path`, `import_options` (typed). |
| **returns** | `imported_assets[]`. |
| **side_effects** | disk |
| **permission** | `write` |
| **ue_entry_points** | `FAssetImportTask`, `UAssetTools::Get().ImportAssetTasks`. Module: `AssetTools`, `UnrealEd`. |
| **threading** | Game thread; may dispatch async import. |
| **failure_modes** | Unsupported format; DDC thrash. |
| **doc_links** | [Automated Asset Import](https://dev.epicgames.com/documentation/en-us/unreal-engine/automated-asset-import-in-unreal-engine) |
| **status** | `future` |

---

## Blueprints & graph tooling

**Layout + merge (append new logic to existing events like Tick instead of duplicating):** in-process **`FUnrealBlueprintGraphFormatService`** (`Plugins/UnrealAiEditor/.../Private/BlueprintFormat/`) drives `auto_layout`, `layout_scope: full_graph`, and **`blueprint_format_graph`**.

### `blueprint_export_ir`

| Field | Value |
|-------|--------|
| **summary** | Lossy JSON export of a script graph (`nodes`, `links`, `defaults`) for planning edits with **`blueprint_apply_ir`** and/or **`blueprint_graph_patch`**. Unknown/custom nodes may include **`k2_class`** + **`node_guid`** for pin introspection and patching. |
| **parameters** | `blueprint_path`, optional `graph_name`. |
| **returns** | `ir` object; `event_tick` and `event_begin_play` ops for matching `UK2Node_Event` nodes. |
| **side_effects** | none |
| **permission** | `read` |
| **status** | `implemented` |

### `blueprint_apply_ir`

| Field | Value |
|-------|--------|
| **summary** | Materialize IR nodes and wire links; optional **`merge_policy`** (`append_to_existing` / `create_new`), **`layout_scope`** (`ir_nodes` / `full_graph`), **`event_tick`** op. |
| **returns** | `merge_policy_used`, `layout_scope_used`, `anchors_reused[]`, `merge_warnings[]`, `layout_applied`, `formatter_hint` (if layout skipped). |
| **side_effects** | asset; compile at end of apply. |
| **permission** | `write` |
| **status** | `implemented` |

### `blueprint_graph_patch`

| Field | Value |
|-------|--------|
| **summary** | Structured **`ops[]`** on a **`/Game`** script graph: **`create_node`**, **`create_comment`** (`member_node_refs` + reflow like IR), **`connect`**, **`break_link`**, **`splice_on_link`** (insert on one exec edge), **`set_pin_default`**, **`add_variable`**, **`remove_node`**, **`move_node`**. Optional **`auto_layout`** (default **true**), **`layout_scope`** `patched_nodes` \| **`full_graph`**. Formatter options match **`blueprint_apply_ir`** (Editor Preferences → Unreal AI Editor → Blueprint Formatting). **`compile`** default true. |
| **parameters** | Full JSON Schema **`oneOf`** per op in [`UnrealAiToolCatalog.json`](../../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json). **`patch_id`** is batch-local; disk nodes use **`guid:`** from export or **`applied[].node_guid`**. |
| **returns** | Success: `ok`, `applied[]`, `blueprint_status`, `compiled`, **`auto_layout`**, **`layout_scope`**, **`layout_applied`**, **`layout_nodes_positioned`**, **`formatter_available`**. Failure: `status` **`patch_errors`**, `errors[]`, **`error_codes[]`**, **`applied_partial`** always **`[]`** (transaction cancelled), **`note`**, **`suggested_correct_call`**. |
| **side_effects** | asset; layout; compile (optional) |
| **permission** | `write` |
| **ue_entry_points** | `UBlueprint`, `UEdGraph`, `UEdGraphSchema_K2`, `FUnrealBlueprintGraphFormatService`, `UnrealBlueprintCommentReflow`, `FBlueprintEditorUtils`, `FKismetEditorUtilities::CompileBlueprint` |
| **threading** | game thread |
| **failure_modes** | `/Game` policy; invalid `k2_class`; connect/break/splice pin errors; failures roll back the whole batch (no partial persistence). |
| **status** | `implemented` |

### `blueprint_graph_list_pins`

| Field | Value |
|-------|--------|
| **summary** | Read-only **`pins[]`** for one graph node: pin **`name`**, **`direction`**, **`category`**, optional **`default_value`**. |
| **parameters** | `blueprint_path`, optional `graph_name`, and **`node_ref` _or_ `guid`** (real graph GUID from export / patch output—not an ephemeral **`patch_id`** from another call). |
| **returns** | `ok`, `node_guid`, `k2_class`, `pins[]`. |
| **side_effects** | none |
| **permission** | `read` |
| **status** | `implemented` |

### `blueprint_format_graph`

| Field | Value |
|-------|--------|
| **summary** | Run full-graph formatter on one script graph (`LayoutEntireGraph`). |
| **parameters** | `blueprint_path`, optional `graph_name` (defaults to first ubergraph). |
| **returns** | `layout_applied`, `layout_nodes_positioned`, `formatter_available`. |
| **side_effects** | asset |
| **permission** | `write` |
| **status** | `implemented` |

### `blueprint_compile`

| Field | Value |
|-------|--------|
| **summary** | Compile a Blueprint asset and return diagnostics. |
| **parameters** | `blueprint_path`. |
| **returns** | `success`, `errors[]`, `warnings[]`. |
| **side_effects** | compile |
| **permission** | `write` |
| **ue_entry_points** | `FKismetEditorUtilities::CompileBlueprint`, `IBlueprintEditor`; message log. Module: `Kismet`, `UnrealEd`. |
| **threading** | Game thread. |
| **failure_modes** | Compile errors. |
| **doc_links** | [Blueprint compilation](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprints-visual-scripting-in-unreal-engine) |
| **status** | `research` |

### `blueprint_get_graph_summary`

| Field | Value |
|-------|--------|
| **summary** | Export a text/JSON summary of nodes in a graph (functions, variables refs). |
| **parameters** | `blueprint_path`, `graph_name`. |
| **returns** | `summary_text` (bounded size). |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `UBlueprint::UbergraphPages`, `FBlueprintEditorUtils`, iterate `UEdGraphNode`. |
| **threading** | Game thread. |
| **failure_modes** | Graph too large — truncate. |
| **doc_links** | [UBlueprint](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UBlueprint) |
| **status** | `research` |

### `blueprint_add_variable`

| Field | Value |
|-------|--------|
| **summary** | Add a member variable to Blueprint (typed). |
| **parameters** | `blueprint_path`, `name`, `type`, `category`. |
| **returns** | `success`. |
| **side_effects** | asset |
| **permission** | `write` |
| **ue_entry_points** | `FBlueprintEditorUtils::AddMemberVariable`, schema validation. |
| **threading** | Game thread. |
| **failure_modes** | Name clash; unsupported type. |
| **doc_links** | [Blueprint editing](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprint-api-in-unreal-engine) |
| **status** | `future` |

### `blueprint_open_graph_tab`

| Field | Value |
|-------|--------|
| **summary** | Open Blueprint editor focused on a specific graph. |
| **parameters** | `blueprint_path`, `graph_name`. |
| **returns** | `success`. |
| **side_effects** | UI |
| **permission** | `write` |
| **ue_entry_points** | `UAssetEditorSubsystem`, `FKismetEditorUtilities`. |
| **threading** | Game thread. |
| **failure_modes** | Graph missing. |
| **doc_links** | [Kismet utilities](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FKismetEditorUtilities) |
| **status** | `research` |

---

## Materials & rendering

### `material_instance_set_scalar_parameter`

| Field | Value |
|-------|--------|
| **summary** | Set scalar parameter on a `UMaterialInstanceConstant` (editor). |
| **parameters** | `material_path`, `parameter_name`, `value`. |
| **returns** | `success`. |
| **side_effects** | asset |
| **permission** | `write` |
| **ue_entry_points** | `UMaterialInstanceDynamic` / constant — `SetScalarParameterValueEditorOnly` patterns; mark package dirty. |
| **threading** | Game thread. |
| **failure_modes** | Parameter not found. |
| **doc_links** | [Material Instances](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-instance-in-unreal-engine) |
| **status** | `research` |

### `material_instance_set_vector_parameter`

| Field | Value |
|-------|--------|
| **summary** | Set vector parameter on material instance. |
| **parameters** | `material_path`, `parameter_name`, `linear_color`. |
| **returns** | `success`. |
| **side_effects** | asset |
| **permission** | `write` |
| **ue_entry_points** | Same as scalar, vector APIs. |
| **threading** | Game thread. |
| **status** | `research` |

### `material_get_usage_summary`

| Field | Value |
|-------|--------|
| **summary** | List which assets reference a material (dependency query). |
| **parameters** | `material_path`. |
| **returns** | `referencers[]`. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `IAssetRegistry::GetReferencers`. |
| **threading** | Game thread. |
| **status** | `research` |

---

## Animation, Sequencer & cinematics

### `sequencer_open`

| Field | Value |
|-------|--------|
| **summary** | Open Level Sequence asset in Sequencer. |
| **parameters** | `sequence_path`. |
| **returns** | `success`. |
| **side_effects** | UI |
| **permission** | `write` |
| **ue_entry_points** | `UAssetEditorSubsystem`, Sequencer module APIs. Module: `Sequencer` / `LevelSequenceEditor`. |
| **threading** | Game thread. |
| **failure_modes** | Module not loaded. |
| **doc_links** | [Sequencer](https://dev.epicgames.com/documentation/en-us/unreal-engine/cinematics-and-movies-in-unreal-engine) |
| **status** | `future` |

### `animation_blueprint_get_graph_summary`

| Field | Value |
|-------|--------|
| **summary** | Summarize AnimGraph / state machine (bounded). |
| **parameters** | `anim_blueprint_path`. |
| **returns** | `summary_text`. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `UAnimBlueprint`, animation editor utilities. |
| **threading** | Game thread. |
| **status** | `future` |

---

## Landscape, foliage & PCG (staged)

| `tool_id` | Summary | Permission | status |
|-----------|---------|------------|--------|
| `landscape_import_heightmap` | Import heightmap to landscape. | `write` | `future` |
| `foliage_paint_instances` | Add foliage instances in radius. | `write` | `future` |
| `pcg_generate` | Execute PCG graph in editor. | `write` | `future` |

**UE surfaces:** `ALandscape`, `UFoliageType`, `UPCGComponent` — require domain-specific QA; ship after core tools.

---

## Physics & collision (staged)

| `tool_id` | Summary | Permission | status |
|-----------|---------|------------|--------|
| `collision_trace_editor_world` | Line/sphere trace in editor world. | `read` | `future` |
| `physics_impulse_actor` | Apply impulse in PIE, not core editor — prefer PIE tools. | `write` | `future` |

---

## Audio & MetaSounds (staged)

| `tool_id` | Summary | Permission | status |
|-----------|---------|------------|--------|
| `metasound_open_editor` | Open MetaSound source. | `write` | `future` |
| `audio_component_preview` | Preview sound in editor. | `write` | `future` |

---

## PIE & play sessions

### `pie_start`

| Field | Value |
|-------|--------|
| **summary** | Start PIE session (selected mode). |
| **parameters** | `mode` (`viewport` \| `standalone` — allow-listed). |
| **returns** | `success`, `session_id`. |
| **side_effects** | play |
| **permission** | `exec` |
| **ue_entry_points** | `UEditorEngine::PlayInEditor`, `RequestPlaySession` patterns; `GUnrealEd`. |
| **threading** | Game thread. |
| **failure_modes** | Already playing; compile errors. |
| **doc_links** | [Play In Editor](https://dev.epicgames.com/documentation/en-us/unreal-engine/play-in-editor-in-unreal-engine) |
| **status** | `research` |

### `pie_stop`

| Field | Value |
|-------|--------|
| **summary** | Stop current PIE session. |
| **parameters** | none |
| **returns** | `success`. |
| **side_effects** | play |
| **permission** | `exec` |
| **ue_entry_points** | `GEditor->EndPlayMap`; engine play commands. |
| **threading** | Game thread. |
| **status** | `research` |

### `pie_status`

| Field | Value |
|-------|--------|
| **summary** | Return whether PIE is active and basic world name. |
| **parameters** | none |
| **returns** | `is_playing`, `world_name`. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `GEditor->PlayWorld`, `GIsPlayInEditor`. |
| **threading** | Game thread. |
| **status** | `research` |

---

## Build, cook & packaging

| `tool_id` | Summary | Permission | status |
|-----------|---------|------------|--------|
| `cook_content_for_platform` | Run Unreal Automation / UAT cook for a platform. | `exec` | `future` |
| `package_project` | Full package build. | `exec` | `future` |
| `shader_compile_wait` | Force wait for shader compile — rarely needed. | `exec` | `future` |

**Note:** Long-running, machine-specific, and CI-oriented — **exclude from v1** default allow-list; if ever enabled, run in subprocess with progress + cancel per PRD safety.

---

## Diagnostics, logs & audit

### `engine_message_log_read`

| Field | Value |
|-------|--------|
| **summary** | Read recent Message Log entries (filtered by category). |
| **parameters** | `category`, `max_lines`. |
| **returns** | `lines[]`. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `FMessageLogModule`, `FTokenizedMessage` iteration — or tail engine log file from `FPaths::ProjectLogDir()`. |
| **threading** | Game thread / async file read for log tail. |
| **failure_modes** | Log file locked. |
| **doc_links** | [Output Log](https://dev.epicgames.com/documentation/en-us/unreal-engine/logging-in-unreal-engine) |
| **status** | `research` |

### `tool_audit_append`

| Field | Value |
|-------|--------|
| **summary** | Append structured line to `Saved/Logs/<Product>_ToolAudit.log` per PRD §5.7. |
| **parameters** | `tool_id`, `status`, `summary`, `asset_path` (optional). |
| **returns** | `success`. |
| **side_effects** | disk |
| **permission** | `write` |
| **ue_entry_points** | Plugin logging — `FFileHelper`, rotate on size optional. |
| **threading** | Async IO preferred. |
| **status** | `designed` |

### `editor_state_snapshot_read`

| Field | Value |
|-------|--------|
| **summary** | Deterministic JSON snapshot of editor/world selection (integrates with context service). |
| **parameters** | `include_assets`, `include_viewport`. |
| **returns** | `snapshot_json` (bounded). |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | Same sources as `FUnrealAiContextService` — Asset Registry, `GEditor`, viewport. |
| **threading** | Game thread. |
| **doc_links** | [`context-management.md`](../context/context-management.md) |
| **status** | `research` |

---

## Project files & search

### `project_file_read_text`

| Field | Value |
|-------|--------|
| **summary** | Read a text file anywhere under the project directory (`relative_path` from project root; includes `Saved/`, `Intermediate/`, etc.). No `..` escape; resolved path must stay under `FPaths::ProjectDir()`. |
| **parameters** | `relative_path`, `max_bytes`. |
| **returns** | `content` or `truncated`. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `FFileHelper::LoadFileToString` after project-relative resolution (no path escape). |
| **threading** | Async file read OK. |
| **failure_modes** | Path outside project; binary file. |
| **doc_links** | [File I/O](https://dev.epicgames.com/documentation/en-us/unreal-engine/file-management-in-unreal-engine) |
| **status** | `research` |

### `project_file_write_text`

| Field | Value |
|-------|--------|
| **summary** | Write a text file anywhere under the project directory (same path rules as `project_file_read_text`; confirmation unless headed auto-run). |
| **parameters** | `relative_path`, `content`, optional `confirm` (auto-filled in headed runs when `AutoRunDestructiveDefault` is true in `UnrealAiRuntimeDefaults.h`). |
| **returns** | `success`. |
| **side_effects** | disk |
| **permission** | `destructive` |
| **ue_entry_points** | `FFileHelper::SaveStringToFile` after project-relative resolution (same rules as read). |
| **threading** | Game or worker thread with flush. |
| **failure_modes** | Read-only VCS. |
| **status** | `future` |

### `source_search_symbol`

| Field | Value |
|-------|--------|
| **summary** | Search C++ / project files for symbol string (deterministic grep, not vector). |
| **parameters** | `query`, `glob`, `max_hits`. |
| **returns** | `matches[]` with file/line. |
| **side_effects** | none |
| **permission** | `read` |
| **ue_entry_points** | `IFileManager::Get().FindFiles`, read files — **scope to `Source/` and plugin** only. |
| **threading** | Background task. |
| **status** | `research` |

---

## Console & gated execution

### `console_command`

| Field | Value |
|-------|--------|
| **summary** | Default: **`command`** is an **allow-list key** mapped to a fixed `GEngine->Exec` line in `UnrealAiToolDispatch_Console.cpp` (`stat_fps`, `stat_unit`, `stat_gpu`, `r_vsync` + **`args`** `0`/`1`, `viewmode_lit` / `viewmode_unlit` / `viewmode_wireframe`). Optional **legacy wide exec**: Editor Settings `Console command: legacy wide exec` or `UNREAL_AI_CONSOLE_COMMAND_LEGACY_EXEC=1` — then `command` is a raw console line (with optional `args` appended); still blocks quit/exit/shader rebuild/crash substrings. |
| **parameters** | `command` (required), `args` (optional; required for `r_vsync`). |
| **returns** | JSON: `ok`, `executed` (allow-list mode), or `command` + `legacy_wide_exec` (legacy). |
| **side_effects** | Engine/editor console side effects of the executed line. |
| **permission** | `exec` |
| **ue_entry_points** | `GEngine->Exec` |
| **threading** | Game thread. |
| **failure_modes** | Unknown key (default mode); invalid args for `r_vsync`; blocked legacy substring. |
| **doc_links** | [Console commands](https://dev.epicgames.com/documentation/en-us/unreal-engine/console-commands-in-unreal-engine) |
| **status** | See `UnrealAiToolCatalog.json` (`implemented`). |

---

## Intentionally absent tool shapes (no catalog stubs)

These capabilities are **not** exposed as tools in `UnrealAiToolCatalog.json` (older placeholder entries were removed):

| Capability | Approach instead |
|------------|------------------|
| Generic HTTP / arbitrary URL fetch | Use the plugin’s configured LLM/embedding transports only — not an agent-callable `fetch(url)`. |
| Spawn arbitrary OS processes | Not on the default tool surface (high abuse risk). |
| Raw `UEngine::Exec` / user shell string | Default: **allow-list keys only**. Unchecked strings only if **legacy wide exec** is explicitly enabled (settings/env). |
| Arbitrary Python | Not on the default product surface. |
| Delete arbitrary paths (e.g. outside project) | **`asset_delete`**, **`project_file_*`**, and other **project-scoped** mutations only. |

---

## Appendix: subsystems & modules to study

Use this as a reading checklist against your **installed** `Engine/Source` tree.

| Module / area | Typical headers / types | Why |
|---------------|-------------------------|-----|
| **UnrealEd** | `UEditorEngine.h`, `GEditor`, `FEditorDelegates` | Core editor entry, selection, world context. |
| **LevelEditor** | `FLevelEditorModule`, `ILevelEditor`, first active viewport | Level Editor integration, tabs. |
| **ContentBrowser** | `FContentBrowserModule`, sync APIs | Navigate UI to assets. |
| **AssetRegistry** | `IAssetRegistry`, `FARFilter`, `FAssetData` | Deterministic asset queries and baseline indexing signals; complements (does not replace) optional local vector retrieval. |
| **AssetTools** | `IAssetTools`, import/rename/delete | Asset mutations. |
| **UnrealEd / EditorScriptingUtilities** | `UEditorAssetLibrary`, `UGameplayStatics` (limited) | Blueprint-exposed patterns; compare with native C++. |
| **Kismet** | `FKismetEditorUtilities`, `FBlueprintEditorUtils` | Blueprint compile/edit. |
| **Renderer / Engine** | `FScreenshotRequest`, `FViewport`, `ReadPixels` | Captures. |
| **Projects** | `FPaths`, `FModuleManager` | Paths and module load. |
| **Settings** | `UDeveloperSettings`, `GConfig` | Plugin settings. |
| **ToolMenus / Slate** | `UToolMenus`, `Slate`, `FSlateApplication` | Custom UI + command wiring. |

### Suggested engine source search terms

`FocusViewport`, `MoveViewportCameras`, `FrameSelection`, `ReadPixels`, `RequestScreenshot`, `OpenEditorForAsset`, `SyncBrowserToAssets`, `CompileBlueprint`, `GetReferencers`, `PlayInEditor`, `EndPlayMap`.

---

## Consolidated tool index (alphabetical)

`actor_attach_to` · `actor_destroy` · `actor_find_by_label` · `actor_get_transform` · `actor_set_transform` · `actor_set_visibility` · `actor_spawn_from_class` · `animation_blueprint_get_graph_summary` · `asset_delete` · `asset_duplicate` · `asset_get_metadata` · `asset_import` · `asset_registry_query` · `asset_rename` · `asset_save_packages` · `blueprint_add_variable` · `blueprint_compile` · `blueprint_get_graph_summary` · `blueprint_open_graph_tab` · `console_command` · `content_browser_navigate_folder` · `content_browser_sync_asset` · `editor_get_selection` · `editor_set_mode` · `editor_set_selection` · `editor_state_snapshot_read` · `engine_message_log_read` · `foliage_paint_instances` · `global_tab_focus` · `landscape_import_heightmap` · `material_get_usage_summary` · `material_instance_set_scalar_parameter` · `material_instance_set_vector_parameter` · `menu_command_invoke` · `metasound_open_editor` · `outliner_folder_move` · `pcg_generate` · `pie_start` · `pie_status` · `pie_stop` · `project_file_read_text` · `project_file_write_text` · `render_target_readback_editor` · `sequencer_open` · `source_search_symbol` · `tool_audit_append` · `viewport_camera_dolly` · `viewport_camera_get_transform` · `viewport_camera_orbit` · `viewport_camera_pan` · `viewport_camera_pilot` · `viewport_camera_set_transform` · `viewport_capture_delayed` · `viewport_capture_png` · `viewport_frame_actors` · `viewport_frame_selection` · `viewport_set_view_mode`

---

*End of tool registry v1.1 (see JSON catalog in `docs/tools/`).*
