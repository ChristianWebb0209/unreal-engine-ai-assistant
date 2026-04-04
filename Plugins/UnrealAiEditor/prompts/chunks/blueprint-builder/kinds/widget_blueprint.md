# Domain: `widget_blueprint` (UMG Widget Blueprints)

**Script graphs** (e.g. **EventGraph** on Widget Blueprints) can follow the same **`blueprint_*`** Kismet tools as **`script_blueprint`** — including **explicit wiring** (`connect` / `links[]` / full-connection T3D) and **`blueprint_compile` + `blueprint_verify_graph`** after substantive edits.

**Designer**

- **UMG Designer** hierarchy/visual placement is **not** covered by **`blueprint_graph_patch`** for visual widgets. Use **`widget_*`** / editor tools when exposed, or manual layout.

**Handoff**

- Set **`target_kind: widget_blueprint`** when the asset is a **Widget Blueprint** so prompts and tool filtering match expectations.
