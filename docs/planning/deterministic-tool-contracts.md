# Deterministic tool contracts (agent + reviewer)

Tools must be **explicit**: discriminators are **required** in the catalog schema; the resolver **does not** infer `operation`, `capture_kind`, `target`, or `value_kind`. Failures return structured `validation_failed` / resolver errors with `suggested_correct_call` where applicable.

**Source of truth:** [`Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`](../../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json) (`parameters`, `required`, `additionalProperties: false`).

**CI:** [`scripts/Validate-UnrealAiToolCatalog.ps1`](../../scripts/Validate-UnrealAiToolCatalog.ps1) (structural + consistency checks).

---

## Resolver: explicit aliases only

These **canonicalize** legacy synonyms to one catalog id (no fuzzy guessing):

| Requested id | Canonical id |
|--------------|----------------|
| `scene_fuzzy_search.query` | `scene_fuzzy_search` |
| `asset_destroy` | `asset_delete` |

**Fuzzy tool ids:** Unknown ids **never** auto-correct. The resolver returns `Unknown tool_id '…'. Did you mean: …?` with suggestions only.

**Path aliases:** Global normalization maps `path` / `asset_path` → `object_path` where applicable; material family maps to `material_path` and strips duplicate alias keys for strict schemas.

---

## Composite tools (required discriminators)

### `setting_query`

- **Required:** `domain`, `key` (see catalog enum for `domain`).
- **Valid:** `{ "domain": "editor_preference", "key": "editor_focus" }`
- **Invalid:** `{}` — missing required fields.

### `setting_apply`

- **Required:** `domain`, `key`, `value`.

### `viewport_camera_control`

- **Required:** `operation` — one of `dolly`, `orbit`, `pan`, `pilot`, `get_transform`, `set_transform`.
- **Valid:** `{ "operation": "get_transform" }`
- **Invalid:** `{}` or omitting `operation` — do not rely on payload-shape guessing.

### `viewport_capture`

- **Required:** `capture_kind` — `immediate_png` or `after_frames`.

### `viewport_frame`

- **Required:** `target` — `selection` or `actors` (use `actor_paths` when `target` is `actors`).

### `material_instance_set_parameter`

- **Required:** `value_kind` (`scalar` | `vector`), `material_path`, `parameter_name`, plus `value` or `linear_color` per branch.
- **Valid vector:** `{ "value_kind": "vector", "material_path": "/Game/M.MI", "parameter_name": "Tint", "linear_color": [1,0.5,0.25] }`
- **Invalid:** omitting `value_kind` even if `linear_color` is present.

### `asset_graph_query`

- **Required:** `relation` (`referencers` | `dependencies`), `object_path`.

---

## Versioning

`meta.resolver_contract.version` in the catalog bumps when resolver policy changes (e.g. inference removed, fuzzy autocorrect disabled).
