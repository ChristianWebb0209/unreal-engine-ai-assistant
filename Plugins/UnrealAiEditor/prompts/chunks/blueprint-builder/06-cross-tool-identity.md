# Cross-tool identity: graph_patch node refs

**Kismet graph edits use `blueprint_graph_patch` only.** Node references must be one of:

- **`patch_id`** from an earlier **`create_node`** in the **same** `ops[]` batch (wire as `myId.PinName`).
- **`guid:AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE`** using the real **`node_guid`** from **`blueprint_graph_introspect`**.
- **Bare UUID** (same as `guid:` form) when the catalog allows it.

**Do not** use legacy **`__UAI_G_*`** tokens in patch ops — they were for removed T3D tooling and are rejected with a clear error.

**Example `connect`:** after introspect shows `node_guid` `A1B2C3D4-...` with an exec input named `Execute` and a new branch node `patch_id` `n_if`:

`{ "op":"connect", "from":"n_if.Then", "to":"guid:A1B2C3D4-E5F6-7890-ABCD-EF1234567890.Execute" }`

(Paste the real GUID and pin names from tool output.)

For uncertain pin names on an existing node, call **`blueprint_graph_list_pins`** first.
