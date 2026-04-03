# Tooling inventory — Keep / Refactor / Replace (Phase 0)

Living snapshot aligned with [tooling-problem.md](tooling-problem.md) and [deterministic-tool-contracts.md](deterministic-tool-contracts.md). Update when families change.

| Family | Current contract | Determinism risk | Action |
|--------|------------------|------------------|--------|
| **Settings** | `setting_query` / `setting_apply` envelope → resolver → `settings_get` / `settings_set` | Low: explicit `domain` + `key`; legacy tools hidden from agent modes | **Keep**; future: domain registry in C++ if settings multiply |
| **Viewport camera** | `viewport_camera_control` with **required** `operation` enum | Was medium (operation inference); **removed** — agent must name branch | **Keep** strict schema |
| **Viewport capture** | `viewport_capture` with **required** `capture_kind` | Was medium (capture_kind inference); **removed** | **Keep** |
| **Viewport frame** | `viewport_frame` with **required** `target` | Was medium (target inference); **removed** | **Keep** |
| **Materials MI params** | `material_instance_set_parameter` with **required** `value_kind` | Was medium (`value_kind` inference from payload); **removed** | **Keep** |
| **Asset graph** | `asset_graph_query` with **required** `relation` + `object_path` | Low | **Keep** |
| **Blueprint IR / patch** | `blueprint_export_ir`, `blueprint_apply_ir`, `blueprint_graph_patch`, compile | Medium: large payloads; deterministic when schema followed | **Keep** baseline; extend tests on change |
| **Generic assets / factory** | `asset_create`, registry queries | Medium: factory resolution | **Refactor** incrementally with explicit `suggested_correct_call` coverage |
| **Resolver boundary** | Aliases + **no Levenshtein autocorrect** (suggestions only) | Was high (silent tool_id repair) | **Replace** ad-hoc repair with explicit alias table + unknown-tool suggestions |

**Deprecated split tools** (`settings_get`, `viewport_camera_*` branches, etc.): **Replace** in agent-facing flows with family tools; remain in catalog for compatibility and matrix coverage where needed.
