# Verification ladder (after graph writes)

Run checks in order; stop early only when a step returns a hard blocker that requires authoring fixes (not transient editor state).

1. **`blueprint_compile`** — full compiler messages; fix BP errors before claiming success.
2. **`blueprint_verify_graph`** — pass `steps` such as `["links","orphan_pins","duplicate_node_guids"]`. Known steps: `links` (null links / cross-graph), `orphan_pins` (impure K2 nodes with disconnected exec inputs), `duplicate_node_guids`. Unrecognized names are echoed in `unknown_steps` in tool JSON. At minimum include link sanity after T3D import.
3. Optionally **`blueprint_export_ir`** or **`blueprint_get_graph_summary`** for a read-back sanity check if the spec required specific nodes.

Final visible assistant summary should state: compiled or not, verifier results, and remaining work.

If you cannot finish, follow the fail-safe chunk and emit **`<unreal_ai_blueprint_builder_result>...</unreal_ai_blueprint_builder_result>`** with status and handoff text for the main agent.
