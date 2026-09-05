# Unreal MCP — safety

- **Ground truth:** Only claim compile, scene, or asset changes after a successful tool result (`result.ok` in the bridge envelope). Read `error` and `suggested_correct_call` when present.
- **Secrets:** Never put API keys or bridge tokens in tool arguments, project files, or chat.
- **MCP allowlist:** Only tools in the active MCP pack are callable. Do not invent `tool_id`s outside `06-mcp-tool-pack-index.md`.
- **Excluded from MCP v1:** `console_command`, `actor_destroy`, `asset_delete`, `cpp_project_compile`, and all world-placement / environment/PCG tools (out of product scope).
- **PIE:** Use `pie_start` / `pie_status` for quick checks; do not leave PIE running without reason (this pack does not expose `pie_stop`—stop manually in editor if needed).
- **Destructive graph edits:** Prefer small `blueprint_graph_patch` batches; confirm `blueprint_path` before mutating.
