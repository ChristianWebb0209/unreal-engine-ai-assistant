# Unreal AI Editor (Editor Plugin Shell)

**Unreal AI Editor** â€” in-editor AI assistant for Unreal Engine **5.7+** **Editor**. Core behavior ships in-plugin (UI, persistence, tool execution, LLM connectors). **MVP:** no product server or hosted backend â€” see the [repo `README.md`](../../README.md) for positioning and [`docs/README.md`](../../docs/README.md) for documentation index.

## What is implemented

- **Window â†’ Unreal AI** and **Tools â†’ Unreal AI** submenus: Agent Chat, AI Settings, Quick Start, Help, **Debug** (local persistence / threads inspector; does not open on startup).
- **Level Editor main toolbar** (top): **Unreal AI** button opens Agent Chat (Nomad tabs are not pinned to the right sidebar until you open them once and dock).
- **Shortcut:** **Ctrl+K** (default chord for Agent Chat).
- **Nomad tabs** for each surface; dockable like other editor tabs. **Agent Chat** is invoked automatically on editor startup (see **Project Settings â†’ Plugins â†’ Unreal AI Editor â†’ Open Agent Chat on editor startup**). Dock it once where you want it; the editor remembers layout.
- **Agent Chat**: transcript (`FUnrealAiChatTranscript`), tool rows (`SToolCallCard`), reasoning subline (`SThinkingSubline`), assistant streaming + optional typewriter (`SAssistantStreamBlock`), todo panel (`STodoPlanPanel`), **Stop** (same control as Send in the composer) â†’ `CancelTurn`. Streaming defaults on (`UUnrealAiEditorSettings::bStreamLlmChat`). For current harness/testing behavior, see [`docs/HEADLESS_TESTING_PLAYBOOK.md`](../../docs/HEADLESS_TESTING_PLAYBOOK.md).
- **AI Settings** (`Window â†’ Unreal AI â†’ AI Settings`): `plugin_settings.json` (v4: nested **API key sections** with unlimited models each), per-model caps including **`maxAgentLlmRounds`** (max toolâ†”LLM iterations per send, default 16), company presets + provider-specific model picker (search + select known OpenAI-compatible model ids), and session **usage** per model + rough **USD** estimates from a small curated in-code pricing catalog. Cumulative usage in `settings/usage_stats.json`. **Test connection**, **Save** â†’ persistence / LLM reload.
- **Project Settings â†’ Plugins â†’ Unreal AI Editor**: `UUnrealAiEditorSettings` (default agent, auto connect, verbose logging, OpenRouter fields).
- **In-module stubs** (`Private/Backend/` â€” not a separate server process): persistence (writes under `%LOCALAPPDATA%\UnrealAiEditor\` on Windows), chat service (fake stream), model connector (delayed success).
- **Tool catalog + execution:** [`Resources/UnrealAiToolCatalog.json`](Resources/UnrealAiToolCatalog.json) â€” single JSON (`meta` + `tools[]`). **`FUnrealAiToolExecutionHost`** (`Private/Tools/`) implements `IToolExecutionHost::InvokeTool` and dispatches to UE5 handlers via `UnrealAiToolDispatch.cpp` and modular `UnrealAiToolDispatch_*.cpp` units (game thread). **Search:** `scene_fuzzy_search`, `asset_index_fuzzy_search`, `source_search_symbol` (`UnrealAiFuzzySearch.cpp`). **Blueprints:** `blueprint_compile`, `blueprint_export_ir`, `blueprint_apply_ir` (merge_policy / event_tick / layout_scope), `blueprint_format_graph`, summaries, open graph, add variable. **Generic assets:** `asset_create`, `asset_export_properties`, `asset_apply_properties`, dependencies/referencers, and moreâ€”see catalog and router for the full set. Tools without a handler return a structured `not_implemented`.
- **Unreal Blueprint Formatter:** `UnrealAiEditor.uplugin` lists **`UnrealBlueprintFormatter`** as an enabled plugin dependency and `UnrealAiEditor.Build.cs` links the module. **`.\build-editor.ps1`** syncs the canonical repo into `Plugins/UnrealBlueprintFormatter/` (clone or `git pull --ff-only`) before each build. Opt out with **`-SkipBlueprintFormatterSync`**. AI prompts in `prompts/chunks/04-tool-calling-contract.md` describe **`merge_policy`**, **`layout_scope`**, **`blueprint_format_graph`**, and **`blueprint_compile`**â€™s **`format_graphs`** flag so agents use the formatter end-to-end.

## Install into a UE project

**Recommended distribution model (bundled):** ship/install both plugins together:

- `Plugins/UnrealAiEditor/`
- `Plugins/UnrealBlueprintFormatter/`

Copy both folders into `<YourProject>/Plugins/`.

**This repo** already has a minimal UE project at the root (`blank.uproject`) with both plugin folders under `Plugins/`.

Then:

1. Right-click the `.uproject` â†’ **Generate Visual Studio project files** (Windows) or equivalent.
2. Build the **Editor** target.
3. Launch the editor; enable plugins if prompted (**Edit â†’ Plugins â†’ Unreal AI Editor**).

### Create a bundled install zip

From repo root, build a single distributable zip containing both plugins:

```powershell
.\scripts\package-bundled-plugins.ps1
```

Default output:

- `dist/UnrealAiEditor-bundled-plugins.zip`

## Manual verification

1. **Unreal AI** toolbar button or **Window â†’ Unreal AI â†’ Agent Chat** opens without errors.
2. Type text in the composer, **Send** â€” assistant area fills with stub streamed text (or a real LLM response when API keys are configured).
3. **Window â†’ Unreal AI â†’ AI Settings** â†’ **Save** â€” confirm `%LOCALAPPDATA%\UnrealAiEditor\settings\plugin_settings.json` exists (Windows).
4. **Edit â†’ Project Settings â†’ Plugins â†’ Unreal AI Editor** â€” fields visible and save to config.

## Harness testing & agent handoff

**Single doc for prompts, catalog, dispatch, scripts, and escalation:** [`docs/tooling/AGENT_HARNESS_HANDOFF.md`](../../docs/tooling/AGENT_HARNESS_HANDOFF.md). Use it when onboarding a new agent or a fresh iteration thread. **Primary qualitative batches:** `.\tests\long-running-tests\run-long-running-headed.ps1` from repo root.

## Automation tests (Editor, dev builds)

When `WITH_DEV_AUTOMATION_TESTS` is enabled (default for **Development** Editor targets), the module registers Session Frontend tests:

| Test | What it guards |
|------|----------------|
| `UnrealAiEditor.Tools.JsonHelpers` | `UnrealAiToolJson::Ok` / `Error` round-trip (shared JSON contract for all tools). |
| `UnrealAiEditor.Tools.DispatchEditorSmoke` | Router returns structured `not_implemented` for unknown IDs; `editor_get_selection` reaches `GEditor` and returns `actor_paths` / `labels` / `count`. |
| `UnrealAiEditor.Tools.CatalogMatrix` | Every catalog tool ID: dispatch with `{}`; asserts non-empty, parseable JSON with `ok` / `status` / `error`; writes `Saved/UnrealAiEditor/Automation/tool_matrix_last.json`. |
| `UnrealAiEditor.Tools.BlueprintApplyIrContract` | `blueprint_apply_ir` invalid IR and invalid `merge_policy`; `blueprint_format_graph` missing path. |
| `UnrealAiEditor.Harness.*` | Plan DAG parse/validation / ready-set (see `UnrealAiAgentHarnessAutomationTests.cpp`). |

**Run from repo root (headless automation + matrix):**

```powershell
.\build-editor.ps1 -AutomationTests -Headless -SkipBlueprintFormatterSync
```

Artifacts: `Saved/UnrealAiEditor/Automation/tool_matrix_last.json`, editor logs under `Saved/Logs/`. Primary qualitative batches: [`tests/long-running-tests/run-long-running-headed.ps1`](../tests/long-running-tests/run-long-running-headed.ps1). Maintainer entry point: [`docs/tooling/AGENT_HARNESS_HANDOFF.md`](../docs/tooling/AGENT_HARNESS_HANDOFF.md).

**Run in editor:** **Tools â†’ Session Frontend** (or **Test Automation** in some UE versions) â†’ search `UnrealAiEditor`, or console `UnrealAi.RunCatalogMatrix [filter]`.

Catalog matrix tests catch **contract** breaks (empty responses, non-JSON, missing structured fields). They do **not** prove every tool succeeds with minimal args (`bOk` may be false by design). Maps/assets/PIE still need targeted integration tests.

## Out of scope (product)

Hosted product backend, mandatory cloud accounts, separate **required** OS service for core chat (optional local helpers are fine). Features not yet implemented are reflected per tool (`not_implemented` or catalog notes), not listed here.

## Context service

Per-chat **context assembly** (`IAgentContextService` / `FUnrealAiContextService`): attachments, tool-result memory, editor snapshot, `BuildContextWindow` â†’ persisted as `chats/<project_id>/threads/<thread_id>/context.json`. See repo [`docs/context/context-management.md`](../../docs/context/context-management.md).

## Engine module layout

- `Public/` â€” module API, tab IDs, `UUnrealAiEditorSettings`.
- `Private/` â€” Slate widgets/tabs (including **Debug**), style, commands, persistence, LLM HTTP transport, **`Private/Context/`** context service, **`Private/Tools/`** tool catalog load + modular dispatch (`UnrealAiToolDispatch.cpp` router, `UnrealAiToolJson.*`, `UnrealAiToolDispatch_*` including `_Search`, `_BlueprintTools`, `_GenericAssets`, â€¦), module implementation.
- `Resources/` â€” **`UnrealAiToolCatalog.json`** (single tool catalog for the plugin).
