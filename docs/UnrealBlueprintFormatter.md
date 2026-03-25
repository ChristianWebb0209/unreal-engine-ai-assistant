# Unreal Blueprint Formatter (repo sync + checklist)

Unreal AI Editor depends on the `UnrealBlueprintFormatter` editor module for blueprint graph layout and merge assistance.

---
## Build-time dependency

- Canonical formatter source repo: https://github.com/ChristianWebb0209/unreal-engine-blueprint-plugin
- `build-editor.ps1` clones/pulls `Plugins/UnrealBlueprintFormatter/` so the build compiles against the latest formatter `main`.

To build:
- Run `.\build-editor.ps1` from the project root.
- If the formatter folder is missing, the script clones it.
- If the formatter folder exists but is not a git clone, the script expects you to remove/rename it so it can clone.

Skip sync (offline / fork):
- `.\build-editor.ps1 -SkipBlueprintFormatterSync`
- or `UE_SKIP_BLUEPRINT_FORMATTER_SYNC=1`

---
## Without the formatter

`layout` and `blueprint_format_graph` degrade gracefully: `blueprint_apply_ir` can still apply IR and returns a `formatter_hint` when the module is missing.

You can’t remove the compile-time module dependency without changing `UnrealAiEditor.Build.cs` and bridging code, which this repo does not do by default.

---
## Implemented in this repo

- `merge_policy` support on `blueprint_apply_ir`:
  - `append_to_existing` reuses existing event anchors (e.g. `ReceiveBeginPlay` / `ReceiveTick`)
  - maps IR `node_id` to existing UE nodes
  - redirects exec tail links (`Then` / latent completions) and warns on duplicate anchors/policies.
- `layout_scope` support on `blueprint_apply_ir`:
  - `ir_nodes` and `full_graph`
- `event_tick` op:
  - apply + export + catalog enum
- Full-graph layout:
  - `layout_scope: full_graph` on apply
  - `blueprint_format_graph` tool uses `LayoutEntireGraph`

See also:
- `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`
- `Plugins/UnrealAiEditor/prompts/chunks/04-tool-calling-contract.md`

---
## Remaining in the formatter repo (when applicable)

- Re-check the BlueprintEditor toolbar hook on UE 5.7+ in formatter sources.
- The formatter repo’s README/license/uplugin metadata and engine-version matrix may duplicate or link this checklist.

---
## Stretch / future

- Richer exec-tail handling for more latent/multi-exec patterns.
- Finer-grained `layout_scope` inside `FUnrealBlueprintGraphFormatService` if a “touched subgraph only” pass becomes necessary.

