# Asset-type coverage matrix (router truth vs plan)

This matrix reflects **what the editor actually dispatches** today in [`UnrealAiToolDispatch.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch.cpp).  
Catalog [`UnrealAiToolCatalog.json`](../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json) `status` fields are **not** source-of-truth until synced to this list.

**Legend**

- **done** — handler wired in router
- **planned** — in roadmap (generic tools cover many families)
- **n/a** — out of scope for parity doc

## Horizontal (all asset families)

| Operation | Status | Tool id(s) |
|-----------|--------|------------|
| Create asset (generic factory) | done | `asset_create` |
| Export editable properties (reflection) | done | `asset_export_properties` |
| Apply property deltas (reflection) | done | `asset_apply_properties` |
| Save packages | done | `asset_save_packages` |
| Rename | done | `asset_rename` |
| Duplicate | done | `asset_duplicate` |
| Delete | done | `asset_delete` |
| Import | done | `asset_import` |
| Open default editor | done | `asset_open_editor` |
| Registry query | done | `asset_registry_query` |
| Fuzzy asset index | done | `asset_index_fuzzy_search` |
| Referencers / dependencies | done | `asset_find_referencers`, `asset_get_dependencies` |
| Metadata | done | `asset_get_metadata` |

## Blueprints & AnimBP (graphs)

| Operation | Status | Tool id(s) |
|-----------|--------|------------|
| Apply graph IR | done | `blueprint_apply_ir` |
| Export graph IR (round-trip aid) | done | `blueprint_export_ir` |
| Graph summary | done | `blueprint_get_graph_summary` |
| Compile + structured diagnostics | done | `blueprint_compile` |
| Open graph tab | done | `blueprint_open_graph_tab` |
| Add variable | done | `blueprint_add_variable` |
| AnimBP graph summary | done | `animation_blueprint_get_graph_summary` |

## Materials

| Operation | Status | Tool id(s) |
|-----------|--------|------------|
| MI scalar/vector | done | `material_instance_set_scalar_parameter`, `material_instance_set_vector_parameter` |
| Usage / referencers | done | `material_get_usage_summary` |
| Generic material/data authoring | done | `asset_export_properties` / `asset_apply_properties` / `asset_create` |

## Sequencer / MetaSound / PCG (scaffold)

| Operation | Status | Tool id(s) |
|-----------|--------|------------|
| Open Sequencer asset | done | `sequencer_open` |
| Create Level Sequence asset | done | `level_sequence_create_asset` |
| Open MetaSound editor | done | `metasound_open_editor` |
| PCG generate | done | `pcg_generate` |
| Create Level Sequence asset | done | `level_sequence_create_asset` |

## World / editor / search / misc

| Operation | Status | Notes |
|-----------|--------|--------|
| Selection, actors, viewport, PIE, console, project files, search, context, packaging | done | See router |

## How to maintain

- When adding a tool: update **router** and this table in the same PR.
- Optional later: generate this table from a shared `TOOLS.md` or macro list.
