# Domain: `script_blueprint` (standard Blueprints)

Kismet **script graphs** on typical `UBlueprint` assets (Actor, Component, etc.): **EventGraph**, functions, macros where the tool stack applies.

**Workflow**

- Prefer **`blueprint_graph_introspect`** (each pin includes **`linked_to`** — peer `node_guid` + `pin_name`) and **`blueprint_export_graph_t3d`** before large writes; use **`blueprint_export_ir`** / **`blueprint_get_graph_summary`** as needed.
- **Wiring is never implicit:** any batch that **creates** nodes must also **connect** them — **`blueprint_graph_patch`** `connect` ops, **`blueprint_apply_ir`** `links[]`, or **T3D** that still contains **export-style** pin / `LinkedTo` / `CustomProperties` connection material (sparse `Begin Object` blobs → **disconnected** nodes). Do not ship create-only patches and expect edges to appear.
- For large graphs, prefer **`blueprint_t3d_preflight_validate`** → **`blueprint_graph_import_t3d`** (placeholder GUIDs **`__UAI_G_000001__`**, …); use **`blueprint_apply_ir`** / **`blueprint_graph_patch`** when IR/patch is a better fit. **Do not** use `__UAI_G_*` tokens inside **`blueprint_graph_patch`** (see **`06-cross-tool-identity.md`**).
- After substantive edits: **`blueprint_compile`** then **`blueprint_verify_graph`** with steps such as **`links`**, **`orphan_pins`**, **`duplicate_node_guids`** — **fix and retry** until clean or return blocked (fail-safe chunk). Use **`blueprint_format_graph`** / **`blueprint_format_selection`** when layout matters.
- Follow `{{BLUEPRINT_COMMENTS_POLICY}}` and `{{CODE_TYPE_PREFERENCE}}` where applicable.

**Out of scope**

- Do not create brand-new `/Game` Blueprint assets unless the initiating spec says they already exist — if a path is missing, stop and report (see fail-safe chunk).
