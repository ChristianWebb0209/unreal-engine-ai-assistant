# Unreal Blueprint Formatter plugin — remaining work

Source tree (when present): `Plugins/UnrealBlueprintFormatter/`  
This file stays in **this** repo while the plugin folder may be gitignored until you split a dedicated repository.

## High priority

- **Merge / append policy (PRD §6):** `append_to_existing` for `event_begin_play`, `event_tick`, etc.; map IR `node_id` to existing `UK2Node_Event`; resolve **exec tail**; redirect links from `event.then` to tail output; duplicate-event warning + deterministic pick.
- **IR / catalog:** `merge_policy`, optional `layout` flags on `blueprint_apply_ir`; document in `UnrealAiToolCatalog.json` and prompt chunk `04-tool-calling-contract.md`.
- **Unreal AI Editor vs split repo:** If `Plugins/UnrealBlueprintFormatter/` is not in this repo, remove or guard **`UnrealAiEditor.uplugin`** plugin reference and **`UnrealAiEditor.Build.cs`** `UnrealBlueprintFormatter` dependency (or document a submodule / copy step) so clean clones still build.
- **`event_tick` (and export):** Add `op` in apply/export IR path so models can target Tick without hand-rolled nodes.

## Layout & robustness

- **Latent / branch graphs:** Improve exec-tail walk (Delay, multi-exec); cycle handling already warns — consider partial layout messaging.
- **Layout scope:** Optional “layout only touched nodes” vs full graph (PRD G5); align with `layout: false` in IR.
- **Optional tool:** `blueprint_format_graph` (headless) calling `LayoutEntireGraph` / `LayoutNodeSubset`.

## Editor UX

- **Toolbar:** Confirm `AssetEditor.BlueprintEditor.ToolBar` placement on your UE version; adjust hook/section if the button is missing or misplaced.
- **Undo:** Already uses transactions where invoked; verify nested undo with AI apply path.

## Tests & CI

- **`WITH_DEV_AUTOMATION_TESTS`:** Small graphs — empty, Tick + Print, duplicate Tick warning, IR all-zero layout applied.
- **Standalone repo:** `README`, `LICENSE`, `.uplugin` metadata, engine version matrix, copy of this checklist.

## Docs

- Update **`docs/PRD-blueprint-formatter.md`** status when merge/layout phases ship.
- **`docs/tool-registry.md`:** Link formatter plugin + `layout_applied` fields on `blueprint_apply_ir`.
