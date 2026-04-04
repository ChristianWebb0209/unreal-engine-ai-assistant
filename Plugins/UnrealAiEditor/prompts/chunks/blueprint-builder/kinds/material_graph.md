# Domain: `material_graph` (base Material expression graphs)

**Scope:** **UMaterial** assets only — expression nodes, `FExpressionInput` links, and **material root** inputs (`BaseColor`, `Normal`, `MaterialAttributes`, `CustomizedUVs_0` …). This is **not** Kismet / Blueprint graphs.

## Tools (material_graph builder surface)

| Phase | Tool IDs |
|--------|-----------|
| Read / plan | `material_graph_export` (structured: nodes, `inputs[]`, `material_inputs[]`), `material_graph_summarize` (lightweight list), `material_graph_validate` |
| Mutate | `material_graph_patch` (`ops[]`: `add_expression`, `connect`, `connect_material_input`, `disconnect_*`, `delete_expression`, `set_constant3vector`, `set_editor_position`; optional `recompile`) |
| Compile | `material_graph_compile` or `recompile: true` on patch |

**`add_expression` without `connect` / `connect_material_input` leaves unwired nodes** (same class of bug as sparse K2 T3D).

**Do not** call `blueprint_graph_patch`, `blueprint_apply_ir`, `blueprint_graph_import_t3d`, `blueprint_compile`, or other **K2** mutators — they target Blueprint graphs, not Materials.

## Workflow

1. **`material_graph_export`** on the existing base Material path (from the handoff spec).
2. Apply **`material_graph_patch`** in coherent batches. Use **`expression_guid`** values from export or from `add_expression` `op_results`. For **`add_expression`**, set **`class_path`** (e.g. `/Script/Engine.MaterialExpressionConstant3Vector`).
3. **`connect`**: `from_expression_guid`, `to_expression_guid`, **`to_input`** (pin / property name, e.g. `A`, `B`), optional **`from_output`** (empty string = first output).
4. **`connect_material_input`**: wire a node into a root slot — **`material_input`** e.g. `BaseColor`, `MaterialAttributes`, `CustomizedUVs_0`; **`from_output`** may be empty.
5. **`material_graph_compile`** then **`material_graph_validate`** (orphans + `compile_error_or_pending`).

## Handoff / failure

If the task is outside base Material graphs (Material Functions, Substrate-only workflows, MI layer stacks), return **`<unreal_ai_blueprint_builder_result>`** with **status** blocked and a short explanation (`03-fail-safe-handoff.md`).
