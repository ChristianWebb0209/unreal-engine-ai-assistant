# Unreal Blueprint Formatter plugin — checklist (tracked in this repo)

Source tree (when present): `Plugins/UnrealBlueprintFormatter/` (often gitignored; see [`UnrealBlueprintFormatter-dependency.md`](UnrealBlueprintFormatter-dependency.md)).

## Done in Unreal AI Editor (this repo)

- **Merge / append policy:** `merge_policy` on `blueprint_apply_ir` — `append_to_existing` reuses the first matching `ReceiveBeginPlay` / `ReceiveTick`, maps IR `node_id` to the existing `UK2Node_Event`, walks exec tail (`Then` / `Delay.Completed`), redirects `Then` links from IR; warns on duplicate graph anchors and on a second IR duplicate of the same builtin event op.
- **IR / catalog:** `merge_policy`, `layout_scope` (`ir_nodes` | `full_graph`), `event_tick` op; documented in [`UnrealAiToolCatalog.json`](../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json) and [`prompts/chunks/04-tool-calling-contract.md`](../Plugins/UnrealAiEditor/prompts/chunks/04-tool-calling-contract.md).
- **`event_tick`:** apply + export + catalog enum.
- **Layout:** `layout_scope: full_graph` on apply; **`blueprint_format_graph`** tool → `LayoutEntireGraph`.
- **Dependency:** documented in [`UnrealBlueprintFormatter-dependency.md`](UnrealBlueprintFormatter-dependency.md) (keep hard module link; copy/submodule formatter beside UnrealAiEditor).
- **Tests:** `WITH_DEV_AUTOMATION_TESTS` — invalid `merge_policy` parse; `blueprint_format_graph` missing args.
- **Docs:** [`PRD-blueprint-formatter.md`](PRD-blueprint-formatter.md), [`tool-registry.md`](tool-registry.md).

## Remaining in the formatter plugin repo (not always present here)

- **Toolbar:** Re-verify `AssetEditor.BlueprintEditor.ToolBar` hook on UE 5.7+ in formatter sources.
- **Standalone repo:** README, LICENSE, `.uplugin` metadata, engine version matrix (duplicate or link this checklist).

## Stretch / future

- Richer exec-tail for all latent / multi-exec patterns; optional `replace_graph` policy.
- Finer `layout_scope` (e.g. touched subgraph only) inside `FUnrealBlueprintGraphFormatService` if needed beyond `ir_nodes` / `full_graph`.
