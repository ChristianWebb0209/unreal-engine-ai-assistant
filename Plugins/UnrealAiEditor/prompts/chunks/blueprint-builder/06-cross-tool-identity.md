# Cross-tool identity: graph_patch node refs

**Kismet graph edits use `blueprint_graph_patch` only.** Node references must be one of:

- **`patch_id`** from an earlier **`create_node`** in the **same** `ops[]` batch (wire as `myId.PinName`).
- **`guid:AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE`** using the real **`node_guid`** from **`blueprint_graph_introspect`**.
- **Bare UUID** (same as `guid:` form) when the catalog allows it — the resolver also accepts **32-hex compact** (no dashes) and normalizes to `guid:` + canonical Lex form for `connect` / `connect_exec`.

**Do not** use **`__UAI_G_*`** tokens in patch ops (removed T3D path); the host rejects them—use real GUIDs or **`patch_id`** (see **`07-graph-patch-canonical.md`**, Catalog scope).

**Do not** reuse **`node_guid`** from **`validate_only`** **`create_node`** spawns — those nodes are deleted after the dry-run. Use **`patch_id`** only inside the same `ops[]`, or run **`blueprint_graph_introspect`** after a real apply.

**Wrong `graph_name`:** if a guid exists only on another script graph, **`patch_errors`** may include **`guid_found_in_graphs`**; **`blueprint_graph_list_pins`** can resolve the node across uber/function/macro graphs and returns **`resolved_graph_name`** / **`requested_graph_name`** when it had to switch graphs.

**Example `connect`:** after introspect shows `node_guid` `A1B2C3D4-...` with an exec input named `Execute` and a new branch node `patch_id` `n_if`:

`{ "op":"connect", "from":"n_if.Then", "to":"guid:A1B2C3D4-E5F6-7890-ABCD-EF1234567890.Execute" }`

(Paste the real GUID and pin names from tool output.)

For uncertain pin names on an existing node, call **`blueprint_graph_list_pins`** first.
