# Unreal AI Editor (Editor Plugin Shell)

**Unreal AI Editor** — in-editor AI assistant for Unreal Engine **5.7+** **Editor**. Core behavior ships in-plugin (UI, persistence, tool execution, LLM connectors). **MVP:** no product server or hosted backend — see the [repo `README.md`](../../README.md) for positioning and [`docs/README.md`](../../docs/README.md) for documentation index.

## What is implemented

- **Window → Unreal AI** and **Tools → Unreal AI** submenus: Agent Chat, AI Settings, Quick Start, Help, **Debug** (local persistence / threads inspector; does not open on startup).
- **Level Editor main toolbar** (top): **Unreal AI** button opens Agent Chat (Nomad tabs are not pinned to the right sidebar until you open them once and dock).
- **Shortcut:** **Ctrl+K** (default chord for Agent Chat).
- **Nomad tabs** for each surface; dockable like other editor tabs. **Agent Chat** is invoked automatically on editor startup (see **Project Settings → Plugins → Unreal AI Editor → Open Agent Chat on editor startup**). Dock it once where you want it; the editor remembers layout.
- **Agent Chat**: transcript (`FUnrealAiChatTranscript`), tool rows (`SToolCallCard`), reasoning subline (`SThinkingSubline`), assistant streaming + optional typewriter (`SAssistantStreamBlock`), todo panel (`STodoPlanPanel`), **Stop** (same control as Send in the composer) → `CancelTurn`. Streaming defaults on (`UUnrealAiEditorSettings::bStreamLlmChat`). See [`docs/chat-renderer.md`](../../docs/chat-renderer.md).
- **AI Settings** (`Window → Unreal AI → AI Settings`): `plugin_settings.json` (v4: nested **API key sections** with unlimited models each), per-model caps including **`maxAgentLlmRounds`** (max tool↔LLM iterations per send, default 16), company presets + hints, session **usage** per model + rough **USD** estimates when a bundled [Litellm](https://github.com/BerriAI/litellm) `model_prices_and_context_window.json` row matches your model id (`Resources/ModelPricing/`). Cumulative usage in `settings/usage_stats.json`. **Test connection**, **Save** → persistence / LLM reload.
- **Project Settings → Plugins → Unreal AI Editor**: `UUnrealAiEditorSettings` (default agent, auto connect, verbose logging, OpenRouter fields).
- **In-module stubs** (`Private/Backend/` — not a separate server process): persistence (writes under `%LOCALAPPDATA%\UnrealAiEditor\` on Windows), chat service (fake stream), model connector (delayed success).
- **Tool catalog + execution:** [`Resources/UnrealAiToolCatalog.json`](Resources/UnrealAiToolCatalog.json) — single JSON (`meta` + `tools[]`). **`FUnrealAiToolExecutionHost`** (`Private/Tools/`) implements `IToolExecutionHost::InvokeTool` and dispatches to UE5 handlers via `UnrealAiToolDispatch.cpp` and modular `UnrealAiToolDispatch_*.cpp` units (game thread). **Search:** `scene_fuzzy_search`, `asset_index_fuzzy_search`, `source_search_symbol` (`UnrealAiFuzzySearch.cpp`). **Blueprints:** `blueprint_compile`, `blueprint_export_ir`, `blueprint_apply_ir` (merge_policy / event_tick / layout_scope), `blueprint_format_graph`, summaries, open graph, add variable. **Generic assets:** `asset_create`, `asset_export_properties`, `asset_apply_properties`, dependencies/referencers, and more—see catalog and router for the full set. Tools without a handler return a structured `not_implemented`.
- **Unreal Blueprint Formatter:** `UnrealAiEditor.uplugin` lists **`UnrealBlueprintFormatter`** as an enabled plugin dependency and `UnrealAiEditor.Build.cs` links the module. **`.\build-editor.ps1`** syncs the canonical repo into `Plugins/UnrealBlueprintFormatter/` (clone or `git pull --ff-only`) before each build; see [`docs/UnrealBlueprintFormatter.md`](../../docs/UnrealBlueprintFormatter.md). Opt out with **`-SkipBlueprintFormatterSync`** / **`UE_SKIP_BLUEPRINT_FORMATTER_SYNC`**. AI prompts in `prompts/chunks/04-tool-calling-contract.md` describe **`merge_policy`**, **`layout_scope`**, **`blueprint_format_graph`**, and **`blueprint_compile`**’s **`format_graphs`** flag so agents use the formatter end-to-end.

## Install into a UE project

**This repo** already has a minimal UE project at the root (`blank.uproject`) with this plugin under `Plugins/UnrealAiEditor/`. Open that `.uproject` to build and run.

**Another project:** copy the `UnrealAiEditor` plugin folder to `<YourProject>/Plugins/UnrealAiEditor/`, then:

1. Right-click the `.uproject` → **Generate Visual Studio project files** (Windows) or equivalent.
2. Build the **Editor** target.
3. Launch the editor; enable the plugin if prompted (**Edit → Plugins → Unreal AI Editor**).

## Manual verification

1. **Unreal AI** toolbar button or **Window → Unreal AI → Agent Chat** opens without errors.
2. Type text in the composer, **Send** — assistant area fills with stub streamed text (or a real LLM response when API keys are configured).
3. **Window → Unreal AI → AI Settings** → **Save** — confirm `%LOCALAPPDATA%\UnrealAiEditor\settings\plugin_settings.json` exists (Windows).
4. **Edit → Project Settings → Plugins → Unreal AI Editor** — fields visible and save to config.

## Harness testing & agent handoff

**Single doc for prompts, catalog, dispatch, scripts, and escalation:** [`docs/AGENT_HARNESS_HANDOFF.md`](../../docs/AGENT_HARNESS_HANDOFF.md). Use it when onboarding a new agent or a fresh iteration thread. **Headed smoke** (visible editor + matrix + two `UnrealAi.RunAgentTurn` scenarios): `.\build-editor.ps1 -HeadedScenarioSmoke` from repo root.

## Automation tests (Editor, dev builds)

When `WITH_DEV_AUTOMATION_TESTS` is enabled (default for **Development** Editor targets), the module registers Session Frontend tests:

| Test | What it guards |
|------|----------------|
| `UnrealAiEditor.Tools.JsonHelpers` | `UnrealAiToolJson::Ok` / `Error` round-trip (shared JSON contract for all tools). |
| `UnrealAiEditor.Tools.DispatchEditorSmoke` | Router returns structured `not_implemented` for unknown IDs; `editor_get_selection` reaches `GEditor` and returns `actor_paths` / `labels` / `count`. |
| `UnrealAiEditor.Tools.CatalogMatrix` | Every catalog tool ID: dispatch with `{}` or `tests/fixtures/<tool_id>.json`; asserts non-empty, parseable JSON with `ok` / `status` / `error`; writes `Saved/UnrealAiEditor/Automation/tool_matrix_last.json`. |
| `UnrealAiEditor.Tools.BlueprintApplyIrContract` | `blueprint_apply_ir` invalid IR and invalid `merge_policy`; `blueprint_format_graph` missing path. |
| `UnrealAiEditor.Harness.*` | Orchestrator merge, DAG parse/validation (see `UnrealAiAgentHarnessAutomationTests.cpp`). |

**Run from repo root (log + matrix JSON for LLM workflows):**

```powershell
.\tests\run-unreal-ai-tests.ps1
# optional: .\tests\run-unreal-ai-tests.ps1 -Build
# optional: .\tests\run-unreal-ai-tests.ps1 -MatrixFilter blueprint
```

Artifacts: [`tests/out/editor-last.log`](../tests/out/editor-last.log), [`tests/out/last-matrix.json`](../tests/out/last-matrix.json) (copy of the Saved report). See [`tests/README.md`](../tests/README.md).

**Run in editor:** **Tools → Session Frontend** (or **Test Automation** in some UE versions) → search `UnrealAiEditor`.

Catalog matrix tests catch **contract** breaks (empty responses, non-JSON, missing structured fields). They do **not** prove every tool succeeds with minimal args (`bOk` may be false by design). Add fixtures under `tests/fixtures/` for deeper per-tool checks; maps/assets/PIE still need targeted integration tests.

## Out of scope (product)

Hosted product backend, mandatory cloud accounts, separate **required** OS service for core chat (optional local helpers are fine). Features not yet implemented are reflected per tool (`not_implemented` or catalog notes), not listed here.

## Context service

Per-chat **context assembly** (`IAgentContextService` / `FUnrealAiContextService`): attachments, tool-result memory, editor snapshot, `BuildContextWindow` → persisted as `chats/<project_id>/threads/<thread_id>/context.json`. See repo [`docs/context-management.md`](../../docs/context-management.md).

## Engine module layout

- `Public/` — module API, tab IDs, `UUnrealAiEditorSettings`.
- `Private/` — Slate widgets/tabs (including **Debug**), style, commands, persistence, LLM HTTP transport, **`Private/Context/`** context service, **`Private/Tools/`** tool catalog load + modular dispatch (`UnrealAiToolDispatch.cpp` router, `UnrealAiToolJson.*`, `UnrealAiToolDispatch_*` including `_Search`, `_BlueprintTools`, `_GenericAssets`, …), module implementation.
- `Resources/` — **`UnrealAiToolCatalog.json`** (single tool catalog for the plugin).
