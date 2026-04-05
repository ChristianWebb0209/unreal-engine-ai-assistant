# Domain: `script_blueprint` (standard Blueprints)

Kismet **script graphs** on typical `UBlueprint` assets (Actor, Component, etc.): **EventGraph**, functions, macros where the tool stack applies.

**Workflow**

- Prefer **`blueprint_graph_introspect`** (each pin includes **`linked_to`** — peer `node_guid` + `pin_name`) and **`blueprint_get_graph_summary`** before large writes.
- **Wiring is never implicit:** any batch that **creates** nodes must also **`connect`** (or **`connect_exec`** when a single exec-out → exec-in is unambiguous) or use **`break_link`** / **`splice_on_link`** as needed. Do not ship create-only patches and expect edges to appear. Prefer **`splice_on_link`** when inserting into an **existing** wired chain (see **`07-graph-patch-canonical.md`**).
- For large op lists, use **`ops_json_path`** (UTF-8 JSON array under `Saved/` or `harness_step/`). **Do not** use legacy `__UAI_G_*` tokens in patch refs (see **`06-cross-tool-identity.md`**).
- After substantive edits: **`blueprint_compile`** then **`blueprint_verify_graph`** with steps such as **`links`**, **`orphan_pins`**, **`duplicate_node_guids`**, **`dead_exec_outputs`**, **`pin_type_mismatch`** — **fix and retry** until clean or return blocked (fail-safe chunk). Use **`blueprint_format_graph`** with **`format_scope`** **`full_graph`** or **`selection`** when layout matters; adjust spacing / preserve-existing / knots in **Editor Preferences → Unreal AI Editor → Blueprint Formatting** (or the Blueprint toolbar AI format options) rather than tool arguments.
- Follow `{{BLUEPRINT_COMMENTS_POLICY}}` and `{{CODE_TYPE_PREFERENCE}}` where applicable.

**Out of scope**

- Do not create brand-new `/Game` Blueprint assets unless the initiating spec says they already exist — if a path is missing, stop and report (see fail-safe chunk).
