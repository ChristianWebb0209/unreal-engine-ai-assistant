# Deterministic Blueprint loop

Prefer the **T3D + placeholder** pipeline (see **`04-t3d-placeholders-and-import.md`**) for whole-graph or large batch authoring: one payload, atomic import, deterministic GUID assignment.

Fallback order when T3D is a poor fit:

1. **Read**: `blueprint_graph_introspect`, `blueprint_export_ir`, `blueprint_export_graph_t3d`, or `blueprint_get_graph_summary` — ground pins and paths in tool output.
2. **Plan**: one coherent T3D generation or one `blueprint_graph_patch` / `blueprint_apply_ir` batch; avoid dozens of micro calls.
3. **Apply**: `blueprint_graph_import_t3d` (preferred for big graphs) or patch / IR.
4. **Validate**: verification ladder in **`05-verification-ladder.md`**.
5. **Layout** (optional): `blueprint_format_graph` when readability matters — formatter options are **Editor Preferences → Unreal AI Editor → Blueprint Formatting** (not tool JSON); T3D import runs a post-import layout pass unless **`skip_post_import_format`** is set.
6. **Report**: what compiled, what failed, what remains; use **`<unreal_ai_blueprint_builder_result>...</unreal_ai_blueprint_builder_result>`** when returning control to the main agent.

Avoid empty `{}` tool calls; honor **`suggested_correct_call`** and **`validation_errors`** from errors; do not invent object paths that never appeared in discovery or exports.

**Schema vs graph:** **`arguments failed schema validation`** is decided in the **tool resolver** against the catalog—**before** Unreal reads the graph. Wrong **`ops[]`** shapes (e.g. **`connect`** without **`from`/`to`** strings, or extra unknown keys) are the usual cause—not Blueprint layout or formatting.

**On failure, do not** pivot to long **manual “click in the editor…”** instructions unless the **user asked** for that or mutators are unavailable. **Do** fix JSON using **`validation_errors`**, introspect/export, smaller patches, and retry.
