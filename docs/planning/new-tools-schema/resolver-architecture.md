# Resolver architecture for `newtoolschema.json`

This document specifies how **dispatch resolvers** should change so the plugin can adopt the vNext catalog **without** asking the LLM to be perfect. The design is **intentionally over-engineered**: most complexity lives **before** the legacy handler, so incorrect or sloppy model output is **normalized, repaired, or rejected with actionable feedback** whenever possible.

Companion schema: [`newtoolschema.json`](newtoolschema.json). Parent planning: [`../optimized-tools-catalog-pitch.md`](../optimized-tools-catalog-pitch.md), [`../tools-catalog-settings-unification.md`](../tools-catalog-settings-unification.md).

---

## 1. Goals (ordered)

1. **Accuracy** — After resolution, arguments must match what legacy C++ handlers expect, or the call fails with **`suggested_correct_call`** that is **machine-actionable** (not prose-only).
2. **Robustness** — Accept common model mistakes: wrong key aliases, swapped `path` vs `object_path`, empty `{}`, near-typos in `tool_id`, missing discriminators on merged tools.
3. **Observability** — Log resolver **stage**, **transforms applied**, **confidence**, and **legacy tool_id emitted** for harness JSONL and long-running tests.

Context reduction is a **catalog + retrieval** concern; resolvers **do not** shrink the prompt. They **increase** implementation surface so the **same** model mistakes cause fewer bad handler runs.

---

## 2. Placement in the pipeline

```
LLM → unreal_ai_dispatch { tool_id, arguments }
        → [Resolver pipeline] → { legacy_tool_id, legacy_arguments, audit }
        → existing dispatch router → FToolHandler / game thread
```

The resolver sits **after** JSON parse, **before** permission checks that assume legacy shape (or re-run permission after canonicalization).

---

## 3. Resolver pipeline (stages)

Implement as **ordered stages** with short-circuit errors. Each stage may append to `audit.transforms[]`.

### Stage A — Ingress validation

- Reject non-object `arguments`.
- If `tool_id` missing: fail with `suggested_correct_call` listing valid vNext ids from catalog (or top-N by BM25 from session — optional).
- **Empty `{}` guard:** If `arguments` is `{}` and the **vNext schema** for this `tool_id` has **any** `required` keys, fail immediately (same rule as prompts chunk 04). Do not pass to handlers.

### Stage B — Tool identity normalization

- **Snake_case fold:** lowercase, collapse duplicate underscores.
- **Legacy alias table for tool_id:** Map vNext `viewport_camera_control` → internal routing key; map **deprecated** LLM habits (if any) to current ids.
- **Fuzzy tool_id repair (optional, high caution):** If unknown `tool_id`, compute Levenshtein distance against enabled catalog ≤ 2 **and** BM25 score above threshold against user turn text; if unique, rewrite and set `audit.warnings[]` (“autocorrected tool_id”). If ambiguous, fail with top-3 candidates.

### Stage C — Argument key canonicalization (alias explosion)

Per **AliasTableEntry** rows in resolver config (not in public schema — see §6):

- For each known `tool_id`, copy `alias → canonical` for **top-level keys only** first.
- Nested objects (e.g. Blueprint IR) use **separate** rules in Stage D.

Examples (canonical targets match legacy handlers after rewrite):

| Legacy / confused keys | Canonical |
|------------------------|-----------|
| `filter`, `object_path` (as folder) on `asset_registry_query` | `path_filter` only when semantically a package prefix |
| `path` on `asset_export_properties` | `object_path` |
| `class_path` on `asset_create` | `asset_class` |
| `variable_name` / `variable_type` on `blueprint_add_variable` | `name` / `type` |

**Rule:** Never silently drop a key — if two aliases map to the same canonical key, **last wins** and emit warning.

### Stage D — Path-kind inference and repair

Use `UnrealPathKind` from schema doc + heuristics:

- If key is wrong but **value** matches `/Game/.*\.[A-Za-z0-9_]+` pattern and tool expects **`asset_object_path`**, rewrite key to `object_path` / `blueprint_path`.
- If value looks like `.../MyProject.uproject` or `Source/...`, map to `relative_path` for project file tools.
- If value contains `:PersistentLevel.`, treat as **`actor_world_path`** for actor tools.
- **Content browser:** `content_browser_sync_asset` may accept `path` | `object_path` | `asset_path` — normalize to the **single** key the legacy handler prefers internally, but still pass through all if handler expects tolerance.

### Stage E — Family / composite dispatch (`routing.strategy == legacy_family`)

For vNext merged tools, **do not** hand merged shape to legacy handlers. **Emit:**

1. `legacy_tool_id` — chosen row from `ToolRouting.legacy_targets` using discriminator:
   - `viewport_camera_control.operation` → maps to `viewport_camera_dolly`, `viewport_camera_orbit`, …
   - `viewport_capture.capture_kind` → `viewport_capture_png` | `viewport_capture_delayed`
   - `viewport_frame.target` → `viewport_frame_actors` | `viewport_frame_selection`
   - `asset_graph_query.relation` → `asset_find_referencers` | `asset_get_dependencies`
   - `material_instance_set_parameter.value_kind` → `material_instance_set_scalar_parameter` | `material_instance_set_vector_parameter`
2. `legacy_arguments` — **project** only the fields that legacy tool accepts; strip discriminator keys (`operation`, `capture_kind`, etc.).

**Validation:** If discriminator missing or invalid, fail with **minimal** example object per branch (from schema `enum` lists).

### Stage F — JSON Schema validation (vNext or legacy)

- After canonicalization + family projection, validate against **legacy** parameter schema **for the emitted legacy_tool_id** (the handler truth).
- If validation fails, run **repair loop** (§4) once; if still failing, return errors + `suggested_correct_call` filled with **defaults** for optional fields only when safe.

### Stage G — Permission and safety gates

- Re-evaluate **destructive** / **exec** using **legacy** tool classification.
- Optional: session **rate limits** per destructive tool_id from harness telemetry.

### Stage H — Telemetry

- Emit `audit.resolver_version` from `meta.resolver_contract.version`.
- Emit `audit.legacy_tool_id`, `audit.transforms`, `audit.warnings`.

---

## 4. Repair loop (optional second pass)

Goal: fix **single-field** mistakes without LLM round-trip.

1. **Required field inference from session:** If `object_path` missing but `last_discovery_asset_paths[]` session deque has exactly one entry and tool requires `object_path`, fill and log `audit.inferred_from_session`.
2. **Enum repair:** If `view_mode` close to known enum (case, whitespace), normalize.
3. **Numeric array length:** Pad or trim `location`/`rotation` arrays of length 2 or 5 with explicit warning or fail (never silent pad Z with 0 unless policy allows).

**Do not** infer destructive `confirm` from free text — only from explicit tools policy.

---

## 5. `suggested_correct_call` generation

When returning validation errors, populate:

```json
{
  "ok": false,
  "error": "validation_failed",
  "suggested_correct_call": {
    "tool_id": "<vNext or legacy canonical>",
    "arguments": { }
  }
}
```

Rules:

- **Arguments** must be **minimal valid** for the **stage** that failed (prefer vNext shape if still in resolver).
- Include **only** required fields + one example optional.
- For family tools, prefer **discriminator + one required branch field** over full legacy projection in the suggestion (so the model learns vNext shape).

---

## 6. Configuration artifacts (outside this file, same design)

Implementers should maintain **machine-readable** tables (JSON or C++ constexpr):

- **`ToolAliasTable`** — list of `AliasTableEntry` (see `$defs` in [`newtoolschema.json`](newtoolschema.json)).
- **`FamilyDispatchTable`** — rows: vNext `tool_id`, discriminator path, enum value → legacy `tool_id`, field projection map.
- **`PathHeuristics`** — regex + `UnrealPathKind` → key rewrite rules per tool family.
- **`ToolIdTypoAllowlist`** — optional fuzzy map for Stage B.

Version these with **`meta.resolver_contract.version`** and bump when any table changes.

---

## 7. Future: `setting_registry` strategy

For `setting_query` / `setting_apply` (`SettingEnvelope`):

- Resolver does **not** call a single legacy tool until C++ **domain registry** exists.
- Stages: validate envelope → resolve **domain + key** to **getter/setter delegate** → map to `UDeveloperSettings` / CVAR / INI / viewport session API.
- **Curated key list** per domain — reject unknown keys with **list of nearest keys** (Levenshtein on key catalog).

---

## 8. Testing strategy

- **Unit:** each Stage A–F with golden JSON inputs/outputs.
- **Property:** alias canonicalization is idempotent.
- **Integration:** harness replay of real `run.jsonl` tool calls through resolver only (no live editor).
- **Regression:** every **family** branch must emit **exact** legacy argument object that current `UnrealAiToolCatalog.json` would accept for that legacy id.

---

## 9. Non-goals

- Resolvers **do not** replace **discovery policy** (prompts still say “search before write”).
- Resolvers **do not** fix **wrong** `/Game/...` hallucinations — only **shape** and **key** errors; path truth remains the model’s job unless session inference is explicitly enabled.
