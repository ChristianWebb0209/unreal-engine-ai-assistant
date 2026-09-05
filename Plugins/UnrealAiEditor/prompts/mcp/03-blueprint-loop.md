# Unreal MCP — Blueprint loop

MCP exposes the same catalog `tool_id`s as the in-editor agent (`blueprint_graph_*`, `blueprint_compile`, etc.). Use this loop for graph edits over MCP:

1. **Discover path:** `asset_index_fuzzy_search` or `editor_get_selection` / `editor_state_snapshot_read` — use `object_path` or `blueprint_path` from tool output only.
2. **Read:** `blueprint_get_graph_summary` and/or `blueprint_graph_introspect`; use `blueprint_graph_list_pins` for one node's pins.
3. **Patch:** `blueprint_graph_patch` with explicit `connect` for new nodes (no orphan nodes). Prefer `validate_only: true` on large `ops[]` batches first.
4. **Verify:** `blueprint_compile`, then `blueprint_verify_graph` if needed.
5. **Diagnostics:** `engine_message_log_read` after compile/patch failures.

Ground pin names in introspect output—not memory. For patch semantics (`semantic_kind`, exec vs data wires), follow the same rules as in-editor `blueprint-builder/07-graph-patch-canonical.md` when you have that doc in context.
