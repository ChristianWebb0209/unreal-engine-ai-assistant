# Cross-tool identity: T3D placeholders vs graph_patch vs IR

Three different mechanisms address **node identity** in Blueprint graphs. Mixing them causes confusing failures.

## `__UAI_G_NNNNNN__` (T3D / import only)

- Valid **only** in text passed to **`blueprint_t3d_preflight_validate`** and **`blueprint_graph_import_t3d`** (and in T3D you author before import).
- The editor resolves these tokens to real `FGuid`s during **`ImportNodesFromText`**.
- **Do not** put `__UAI_G_*` tokens inside **`blueprint_graph_patch`** ops (`guid:...`, `move_node`, `connect`, etc.). Patch resolves **`guid:<uuid>`** with a real Unreal GUID string, **`patch_id`** from the same patch batch, or a bare UUID — see tool catalog for `blueprint_graph_patch`.

## `blueprint_graph_patch` references

- Use **`guid:AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE`** (real node GUID from **`blueprint_graph_introspect`** / **`blueprint_export_ir`**), or **`patch_id`** from an earlier op in the **same** `ops[]` batch.
- If you only have T3D placeholders, run **`blueprint_graph_import_t3d`** first, then introspect for real GUIDs before patching.

## `blueprint_apply_ir`

- Uses **`node_id`** strings **you** assign in the IR JSON, plus **`links`** as `node_id.PinName` → `node_id.PinName`.
- Not the same as T3D placeholders; do not paste `__UAI_G_*` into `node_id`.

## Quick decision

| Goal | Prefer |
|------|--------|
| Bulk shape / paste-like edits | T3D export → edit → preflight → import |
| Small surgical K2 edits | `blueprint_graph_patch` with real GUIDs or patch_ids |
| Compact structured nodes (supported ops) | `blueprint_apply_ir` with valid `nodes[]` / `links[]` |
