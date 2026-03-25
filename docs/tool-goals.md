# Tool goals (MVP qualitative scenarios)

Tool-goals are the qualitative “what good looks like” targets that drive the live headed harness and context workflows.

The runnable scenario prompts live under:
- `tests/live_scenarios/` (manifest-driven, headed, real API)
- `tests/context_workflows/` (multi-turn same thread, context manager review)

---
## How this maps to tools

Implementation is driven by **catalog tools** (`Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`), not by one-shot magic:

`search → asset/Blueprint edits → scene placement → PIE`

Blueprint layout + merge preference:
- Prefer `blueprint_export_ir` → `blueprint_apply_ir`
- Use `merge_policy` / `event_tick` / `event_begin_play` as needed
- Let `UnrealBlueprintFormatter` handle `auto_layout` / `layout_scope`
- End with `blueprint_compile` (optionally `format_graphs: true`) or `blueprint_format_graph` for a single graph pass

See: `Plugins/UnrealAiEditor/prompts/chunks/04-tool-calling-contract.md` and `docs/UnrealBlueprintFormatter.md`.

---
## Known tooling gaps (not implemented in v1)

- Input / mapping: no first-class tools for Enhanced Input / `DefaultInput.ini`; workarounds are fragile config edits or manual editor setup.
- UMG / Widget Designer: no widget-specific IR/layout export/apply; HUD-like UI likely remains “manual UMG” until widget IR exists.
- Animation: `animation_blueprint_get_graph_summary` exists, but there is no symmetric apply tool for AnimBP graphs (AnimNotifies often need this).
- AI / Navigation: no dedicated tools for NavMesh building / Behavior Trees / Blackboards.
- Level Sequence: tools can open assets, but not yet author tracks/bindings/keyframes programmatically at scale.
- SaveGame: no `save_game_slot` / `load_game_slot` wrappers; agents must use asset + Blueprint IR/ops directly.
- Editor vs PIE physics: `physics_impulse_actor` is PIE-oriented; editor physics workflows need separate semantics/UX clarity.
- Matrix signal vs failure: catalog matrix calls many tools with `{}` and may return `ok:false` as a normal contract outcome; treat this as expected unless schema/validation is actually wrong.

---
## References

- `tests/live_scenarios/README.md`
- `tests/context_workflows/README.md`

