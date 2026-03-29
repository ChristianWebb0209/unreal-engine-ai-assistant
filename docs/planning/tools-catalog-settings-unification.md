# Tools catalog redesign: unified settings, get/set patterns, and UE exposure

This document captures **product and technical brainstorming** for a possible **redo of the tools catalog** around semantic patterns (for example `get_*` / `set_*` pairs), a **unified settings query/apply** surface with a **typed schema** (viewport session vs project vs editor vs CVARs), and how that maps to **how Unreal Engine actually exposes** configuration. It is planning-only until implemented in `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json` and handlers.

**Related:** Current catalog shape and categories live in the embedded catalog; tool surface metadata is described in catalog `meta` and prompts under `Plugins/UnrealAiEditor/prompts/`.

---

## 1. Goals of the approach

- **Reduce catalog sprawl** by giving the model a small number of **stable contracts**: discover → read → write → verify.
- **Naming contracts** such as `get_*` / `set_*` pairs (or `setting_query` / `setting_apply`) so each “read” has an obvious “write” counterpart where it makes sense.
- **One envelope** for key/value-ish surfaces: **domain + key + scope**, aligned with how UE organizes config (not one flat global map).
- **Fewer bespoke JSON shapes** for the model if validation, permissions, and harness logging go through one path.

---

## 2. Strengths and risks

### 2.1 Strengths

- The model gets a **repeatable mental model** instead of dozens of one-off parameter blobs.
- A **typed envelope** matches Unreal’s structure: parallel systems (INI, console, editor subsystems, asset-backed settings), not a single registry.
- **Auditability:** one place to log before/after, domain, and resolution strategy (CVar vs config vs object property).

### 2.2 Risks and boundaries

- **Not everything is a “setting.”** Viewport mode, PIE, selection, and many actions are **commands**, not persisted config. Folding them into “settings” blurs semantics and complicates undo/rollback stories.
- **Authority differs by domain.** Changing `DefaultEngine.ini` is not the same as toggling a **session-only** CVAR. The tool layer needs **permission tiers** per domain (align with existing catalog ideas such as `permission`, `side_effects`).
- **Schema explosion.** If the unified tool tries to mirror all of UE, it recreates the engine API. The win comes from **a small set of domains**, **curated keys**, and **controlled escape hatches** (for example allowlisted console or raw INI paths).

**Discipline:** unify **persistent or well-scoped editor state** and **session UI/viewport state** as separate domains; keep **relational and graph operations** as verbs (spawn, attach, compile, edit graph).

---

## 3. How Unreal exposes get/set for different kinds of state

Unreal does **not** expose one universal “set any project setting” API. In practice, configuration is layered:

### 3.1 `UDeveloperSettings` and CVAR-backed settings

- Project-wide or editor-wide defaults often appear under **Project Settings** / **Editor Preferences**.
- Typical pattern: `GetDefault<UYourSettings>()` for read; `GetMutableDefault<UYourSettings>()` plus `SaveConfig()` (or equivalent) for write, depending on class.
- Some settings use **`UDeveloperSettingsBackedByCVars`** so CVARs and settings UI stay in sync.

### 3.2 `ISettingsModule` / settings sections

- The **Editor Preferences** and **Project Settings** UIs are built from registered sections.
- Programmatic access is usually still via the **underlying settings object** and/or **config**, not a single generic “set the row shown in the UI” API.

### 3.3 INI files and `GConfig`

- `Config=` on `UCLASS`, sections like `[/Script/Module.Class]`, files such as `DefaultEditor.ini`.
- Often the **source of truth**; many APIs read/write through the config cache.

### 3.4 Console variables (`IConsoleVariable`, `IConsoleManager`)

- Rendering, scalability, debugging: **`IConsoleManager::Get()`** to find variables and set values.
- Some mirror **settings objects**; some are **runtime-only** until something else persists them.

### 3.5 Editor subsystems and singletons

- **Viewport** clients, **PIE**, **Content Browser** filters, **Level Editor** mode: exposed via subsystem APIs, not one settings registry.

### 3.6 Per-asset and per-level data

- **World settings**, post-process volumes, map-specific overrides: often **actors** or **assets**, not global project settings.

### 3.7 Plugins

- Each plugin may register its own `UDeveloperSettings` or config; there is **no stock enumerator of every setting in the engine and all plugins**.

**Implication for a “unified” tool:** implement a **facade** in the plugin that dispatches to these mechanisms, with an explicit **domain registry** in C++.

---

## 4. Building a settings getter/setter (conceptual design)

### 4.1 Tool shapes

- **`setting_query`** (or `get_setting`): `{ domain, key, scope? }` → `{ value, source, readonly?, requires_restart? }`.
- **`setting_apply`** (or `set_setting`): same identifiers plus `{ value }` and optional `{ confirm?, dry_run? }`.
- **`setting_list_keys`** (optional): `{ domain, prefix? }` for discovery. **Naive full enumeration is expensive and noisy**; prefer **curated** lists per domain in the first iterations.

### 4.2 Implementation strategy

1. **Domain registry in C++** (plugin): each domain implements:
   - resolve **key → read path**;
   - **type validation**;
   - **write + persist** strategy (`SaveConfig`, flush `GConfig`, mark packages dirty, etc.);
   - **side-effect** and **permission** classification.
2. **Start narrow:** for example CVAR-backed surfaces, or one `UDeveloperSettings` class, or one allowlisted INI section — prove logging, permissions, and harness behavior before widening.
3. **Escape hatches (controlled):** existing higher-risk paths (for example `console_exec`) remain separate; optional **raw config** access only with **strict allowlists** of sections and keys.
4. **Observability:** log **before/after**, **domain**, **key**, and **resolution path** (CVar vs config vs property) in harness outputs for long-running test batches.

---

## 5. Schema: which “type” of setting

Use a single **envelope** the model learns once; differentiate **lifetime** and **authority** with `domain` and optional `scope`.

### 5.1 Example envelope

```json
{
  "domain": "editor_preference | project_setting | cvar | viewport_session | plugin_setting | map_world_settings",
  "key": "string_or_dot_path",
  "scope": {
    "project_path": "optional",
    "plugin_id": "optional",
    "map_path": "optional",
    "viewport_id": "optional"
  }
}
```

### 5.2 Important splits

- **`project_setting` / `editor_preference`:** usually **persistent** — map to `UDeveloperSettings`, `SaveConfig`, INI as appropriate.
- **`viewport_session`:** **ephemeral** (lit/unlit, show flags, FOV for this viewport) — same *shape* as a setting but different **lifetime** and **permission**; use a distinct `domain` so the model does not assume a disk write occurred.
- **Per-map / per-asset:** `scope` carries `map_path` or asset path when the backing store is not global.

### 5.3 Value typing

Align with UE literals using something like **`value_kind`**: `bool | int | float | string | enum | linear_color | vector`. For enums, prefer **stable string tokens** or documented ints with **allowed values** per key in the catalog or a small sidecar manifest.

---

## 6. Tool categories that fit this pattern well

Surfaces where **read → write → verify** and shared naming pay off:

1. **Console / CVARs** — get/set with **whitelist** by prefix (`r.`, `sg.`, `fx.`, etc.).
2. **Rendering and scalability** — often CVAR + ini; one domain with curated keys.
3. **Editor preferences** — many bools/enums (`UEditorPerProjectUserSettings` and related classes).
4. **Project settings** exposed as `UDeveloperSettings` — packaging defaults, maps, input, collision, navigation, etc.
5. **Plugin settings** — `domain` + `plugin_id` + key per `UDeveloperSettings` in a plugin.
6. **Source control / editor loading** — where exposed as settings objects.
7. **Level editor** — grid snap, units, transform gizmo — only if APIs are stable enough to support.
8. **Viewport (session)** — view mode, exposure, show flags, game view — consider **`get_viewport_state` / `set_viewport_state`** instead of many separate tools.
9. **PIE** — standalone vs viewport, player count — often settings-like.
10. **Content Browser / asset registry** — filters and view modes as **session UI state** — possible `editor_ui_state` domain with short lifetime.
11. **Blueprint editor** — split **per-asset compiler settings** from **global editor prefs**.
12. **World Partition / streaming** — project defaults vs per-world overrides.
13. **Navigation and AI** — project defaults vs level overrides.
14. **Physics** — project defaults vs world override actors.
15. **Audio** — platform ini vs project audio settings.
16. **Localization** — gather settings, cultures — often settings objects + config.
17. **Build and packaging** — `UProjectPackagingSettings` and related — **high value, high risk**; strict schema and confirmation.
18. **Cook / staging** — only where exposed as settings; do not casually mix with command-line-only workflows.
19. **Automation / tests** — editor test settings if exposed and allowlisted.
20. **Diagnostics** — log verbosity, on-screen stats — CVAR-heavy, good fit for a dedicated domain.

---

## 7. Categories that are a poor fit as generic “settings”

Keep these as **verbs** or **specialized tools** with their own schemas:

- **Actor CRUD, transforms, attachments** — relational, not key/value.
- **Sequencer, animation** — timeline and asset operations.
- **Blueprint graph edits** — structured editing.
- **One-shot captures and screenshots** — commands.
- **Search and query** — prefer `search_*` / `query_*` families with query-specific parameters.

---

## 8. Conclusion

**Direction:** A **small set of domains**, a **stable envelope** (`domain` + `key` + `scope` + `value_kind`), explicit **query/apply** (or get/set), and **session vs persistent** encoded in `domain` can shrink the catalog and improve model consistency.

**Guardrail:** Do not unify **everything** — only **key/value-ish** surfaces — and implement **curated registries** per domain so the product does not promise “all of Unreal” behind one JSON blob.

**Next steps (when implementing):** Map existing catalog categories (for example `viewport_camera`, `build_packaging`, `materials_rendering`, `console_exec`) to proposed domains; decide which current tools **merge** into unified settings/query tools and which **stay** as imperative operations.
