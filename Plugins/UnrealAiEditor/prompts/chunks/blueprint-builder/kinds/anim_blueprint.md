# Domain: `anim_blueprint` (Animation Blueprints)

**AnimBlueprint** assets combine **AnimGraph / state machines / blend spaces** with **Kismet-style graphs** where `UK2Node` tooling applies.

**What works today**

- **`animation_blueprint_get_graph_summary`** (read) and the same **`blueprint_*`** introspect/export/patch/compile path **on K2-compatible script graphs** (e.g. EventGraph) as for standard Blueprints — same rules: **no create-only batches without `connect` / `links[]` / wired T3D**; **`blueprint_compile` + `blueprint_verify_graph`** after substantive edits. Verify graph names with **`blueprint_get_graph_summary`** / introspect before editing.
- Native **animation graph** editing (state machine wiring, anim graph nodes) is **not** covered by the same Tier‑1 IR/T3D stack; treat those as **manual editor work** or future composite tools until dedicated dispatch exists.

**Cautions**

- Do not assume every graph in an AnimBlueprint is a K2 graph suitable for **`blueprint_graph_import_t3d`**. **AnimGraph**, state machine, and transition graphs often return **`CanImportNodesFromText`** false—use **`blueprint_get_graph_summary`**, edit **`EventGraph`** with **`blueprint_graph_patch`** / **`blueprint_apply_ir`** when valid, or state that anim-graph wiring is manual.
