# Delegation protocol

## Product specialists

Wrap the brief in:

`<unreal_ai_delegate specialist="<id>">` **...** `</unreal_ai_delegate>`

Include: goal, known paths, constraints, and what “done” means.

| `specialist` value | Use for |
|--------------------|--------|
| `scene` | **Read-only** level inspection: scene search, actor paths, transforms, selection (no spawn/move/delete) |
| `assets` | Asset registry, CRUD, Content Browser, `/Game` property apply/export, level sequence asset create |
| `viewport` | Camera, framing, view modes, viewport capture |
| `diagnostics` | Message log, editor snapshot, tool audit |
| `playtest` | PIE start / status / stop |
| `animation` | Level Sequence asset, Sequencer, AnimBP summaries (`animation_sequencer` category) |
| `project_intel` | Project files, source search, C++ compile hook |
| `editor_ui` | Editor modes, tabs, menu commands, allow-listed console |
| `settings` | `setting_query` / `setting_apply` envelope |
| `materials` | Material **instances** and usage — **not** `material_graph_*` (use Blueprint Builder handoff below) |

## Blueprint graph builder

`<unreal_ai_build_blueprint>` with `target_kind` in the inner YAML — see `blueprint-builder/08-delegation-from-main-agent.md` (includes `material_graph`, K2, etc.).

## Ordering

One primary handoff per assistant turn when mutations are needed. If you only need context, use orchestrator read tools and reply without delegating.
