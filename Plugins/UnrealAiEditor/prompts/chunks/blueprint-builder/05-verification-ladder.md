# Verification ladder (after graph writes)

Run checks in order; stop early only when a step returns a hard blocker that requires authoring fixes (not transient editor state).

1. **`blueprint_compile`** — full compiler messages; fix BP errors before claiming success.
2. **`blueprint_verify_graph`** — pass `steps` such as `["links","orphan_pins","duplicate_node_guids","dead_exec_outputs","pin_type_mismatch","trivial_branch_conditions"]`. Known steps: `links`, `orphan_pins`, `duplicate_node_guids`, `dead_exec_outputs`, `pin_type_mismatch`, `trivial_branch_conditions` (IfThenElse with Condition literal true/false and no data wire). **`issues[]`** entries include stable **`code`**, **`message`**, and **`node_guid`** when the checker tied the finding to one node. Unrecognized names are echoed in **`unknown_steps`**. At minimum include link sanity after substantive patches.
3. Optionally **`blueprint_graph_introspect`** or **`blueprint_get_graph_summary`** for a read-back sanity check if the spec required specific nodes.

Final visible assistant summary should state: compiled or not, verifier results, and remaining work.

If you cannot finish, follow the fail-safe chunk and emit **`<unreal_ai_blueprint_builder_result>...</unreal_ai_blueprint_builder_result>`** with status and handoff text for the main agent.
