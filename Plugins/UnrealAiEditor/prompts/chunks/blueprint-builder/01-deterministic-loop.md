# Deterministic Blueprint loop

**Default:** **`blueprint_graph_patch`** with **`ops[]`** or **`ops_json_path`**. Read first (`blueprint_graph_introspect`, **`blueprint_get_graph_summary`**), then mutate, then verify. Use **`validate_only:true`** on large batches before applying.

Reserve **very small** patches for surgical work (`splice_on_link`, few `create_node` + `connect`, `set_pin_default`, `add_variable`). For larger subgraphs, split into multiple patch calls or use **`ops_json_path`** under `Saved/` / `harness_step/` — not ad-hoc T3D/IR flows (removed from the agent catalog).

Typical order:

1. **Read:** `blueprint_graph_introspect` (pins + `linked_to`), and/or `blueprint_get_graph_summary` — ground pins and paths in tool output. For one node’s pins only, `blueprint_graph_list_pins`.
2. **Plan:** one coherent `blueprint_graph_patch` batch (or `validate_only` dry-run first); avoid dozens of micro calls when one batch with explicit `connect` is clearer.
3. **Apply:** `blueprint_graph_patch` only for Kismet mutations.
4. **Validate:** verification ladder in **`05-verification-ladder.md`** (`blueprint_compile`, `blueprint_verify_graph`).
5. **Layout (optional):** `blueprint_format_graph` with `format_scope` `full_graph` or `selection` when readability matters — formatter options are **Editor Preferences → Unreal AI Editor → Blueprint Formatting** (not tool JSON).
6. **Report:** what compiled, what failed, what remains; use **`<unreal_ai_blueprint_builder_result>...</unreal_ai_blueprint_builder_result>`** when returning control to the main agent.

Avoid empty `{}` tool calls; honor **`suggested_correct_call`** and **`validation_errors`** from resolver failures; on **`blueprint_graph_patch`** runtime failures (`status: patch_errors`), use **`errors[]`**, **`error_codes[]`**, and **`suggested_correct_call`** (and remember **`applied_partial` is always empty**—the patch did not commit). Do not invent object paths that never appeared in discovery or introspect.

**Schema vs graph:** **`arguments failed schema validation`** is decided in the **tool resolver** against the catalog—**before** Unreal reads the graph. Wrong **`ops[]`** shapes (e.g. **`connect`** without **`from`/`to`** strings, or extra unknown keys) are the usual cause—not Blueprint layout or formatting. Unknown **`layout_scope`** / **`layout_anchor`** on **`blueprint_graph_patch`** also fail here (not as **`patch_errors`**).

**On failure, do not** pivot to long **manual “click in the editor…”** instructions unless the **user asked** for that or mutators are unavailable. **Do** fix JSON using **`validation_errors`**, introspect, smaller patches, and retry.
