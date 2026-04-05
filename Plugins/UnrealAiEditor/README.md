# Unreal AI Editor (Editor Plugin Shell)

**Unreal AI Editor** â€” in-editor AI assistant for Unreal Engine **5.7+** **Editor**. Core behavior ships in-plugin (UI, persistence, tool execution, LLM connectors). **MVP:** no product server or hosted backend â€” see the [repo `README.md`](../../README.md) for positioning and [`docs/README.md`](../../docs/README.md) for documentation index.

## What is implemented

- **Window â†’ Unreal AI** and **Tools â†’ Unreal AI** submenus: Agent Chat, AI Settings, Quick Start, Help, **Debug** (local persistence / threads inspector; does not open on startup).
- **Level Editor main toolbar** (top): **Unreal AI** button opens Agent Chat (Nomad tabs are not pinned to the right sidebar until you open them once and dock).
- **Shortcut:** **Ctrl+K** (default chord for Agent Chat).
- **Nomad tabs** for each surface; dockable like other editor tabs. **Agent Chat** is invoked automatically on editor startup (see **Project Settings â†’ Plugins â†’ Unreal AI Editor â†’ Open Agent Chat on editor startup**). Dock it once where you want it; the editor remembers layout.
- **Agent Chat**: transcript (`FUnrealAiChatTranscript`), tool rows (`SToolCallCard`), reasoning subline (`SThinkingSubline`), assistant streaming + optional typewriter (`SAssistantStreamBlock`), todo panel (`STodoPlanPanel`), **run progress** (`RunProgress` rows: status line + optional **Show details** timeline from harness events), **Stop** (same control as Send in the composer) â†’ `CancelTurn`. Streaming defaults on (`UUnrealAiEditorSettings::bStreamLlmChat`). For current harness/testing behavior, see [`docs/HEADLESS_TESTING_PLAYBOOK.md`](../../docs/HEADLESS_TESTING_PLAYBOOK.md).
- **AI Settings** (`Window â†’ Unreal AI â†’ AI Settings`): `plugin_settings.json` (v4: nested **API key sections** with unlimited models each), per-model caps including **`maxAgentLlmRounds`** (max toolâ†”LLM iterations per send, default 16), company presets + provider-specific model picker (search + select known OpenAI-compatible model ids), and session **usage** per model + rough **USD** estimates from a small curated in-code pricing catalog. Cumulative usage in `settings/usage_stats.json`. **Editor integration** includes **`agent.useSubagents`** (**Use subagents**) for plan wave policy. **Test connection**, **Save** â†’ persistence / LLM reload.
- **Project Settings -> Plugins -> Unreal AI Editor**: `UUnrealAiEditorSettings` (default agent, auto connect, verbose logging, OpenRouter fields, **Blueprint comments** Off/Minimal/Verbose for agent prompt injection, **Console command: legacy wide exec** for unsafe raw `GEngine->Exec`).
- **In-module stubs** (`Private/Backend/` â€” not a separate server process): persistence (writes under `%LOCALAPPDATA%\UnrealAiEditor\` on Windows), chat service (fake stream), model connector (delayed success).
- **Tool catalog + execution:** Primary [`Resources/tools.main.json`](Resources/tools.main.json) (`meta` + main roster `tools[]`); at runtime the editor merges [`tools.blueprint.json`](Resources/tools.blueprint.json) and [`tools.environment.json`](Resources/tools.environment.json) from `meta.tool_catalog_fragments` into the full merged `tools[]` (`meta.status_legend` describes `status`). **`FUnrealAiToolExecutionHost`** (`Private/Tools/`) implements `IToolExecutionHost::InvokeTool` and dispatches to UE5 handlers via `UnrealAiToolDispatch.cpp` and modular `UnrealAiToolDispatch_*.cpp` units (game thread). **Main Agent vs Builder surfaces:** default Agent mode uses **`bOmitMainAgentBlueprintMutationTools`** + per-tool **`agent_surfaces`** (`UnrealAiAgentToolGate.cpp`); substantive K2 graph edits run in a **Blueprint Builder** sub-turn after **`<unreal_ai_build_blueprint>`**; PCG/landscape/foliage mutators run in an **Environment Builder** sub-turn after **`<unreal_ai_build_environment>`**. **Escape hatch:** disabling `bOmitMainAgentBlueprintMutationTools` skips surface gating. **Compat aliases:** [`docs/tooling/tool-dispatch-inventory.md`](../../docs/tooling/tool-dispatch-inventory.md). **`console_command`:** allow-list keys by default (`UnrealAiToolDispatch_Console.cpp`); optional legacy wide exec in **Project Settings → Plugins → Unreal AI Editor**. **Search:** `scene_fuzzy_search`, `asset_index_fuzzy_search`, `source_search_symbol` (`UnrealAiFuzzySearch.cpp`). **Blueprints (builder roster):** `blueprint_graph_patch`, `blueprint_apply_ir`, `blueprint_compile`, `blueprint_format_*`, `blueprint_add_variable`, etc. **Blueprints (read / shared):** `blueprint_export_ir`, `blueprint_get_graph_summary`, `blueprint_graph_list_pins`, `blueprint_open_graph_tab`, T3D helpers—see catalog `agent_surfaces`. **Generic assets:** `asset_create`, `asset_export_properties`, `asset_apply_properties`, dependencies/referencers, and more. Tools without a handler return a structured `not_implemented`.
- **Blueprint graph layout:** Vendored in `Source/.../Private/BlueprintFormat/` (MIT attribution in headers). Prompts in `prompts/chunks/common/04-tool-calling-contract.md` describe **`merge_policy`**, **`layout_scope`**, **`blueprint_format_graph`**, and **`blueprint_compile`**'s **`format_graphs`**. **Blueprint comments** mode lives in **Editor Preferences → Plugins → Unreal AI Editor** and is injected into the static prompt (`common/01-identity.md` token).

## Install into a UE project

**Recommended distribution:** ship the plugin folder:

- `Plugins/UnrealAiEditor/`

Copy it into `<YourProject>/Plugins/`.

**This repo** has a UE **First Person BP** (Blueprint-only) sample at the root (default: `TP_FirstPerson.uproject`) with `Plugins/UnrealAiEditor/`. Scripts resolve the root `*.uproject` automatically; use **`UE_REPO_UPROJECT`** when more than one exists.

Then:

1. From repo root: `.\build-editor.ps1 -Headless` (or right-click the `.uproject` → **Generate Visual Studio project files**, then build **`<ProjectName>Editor`** for that manifest).
2. If you skipped the script, ensure **`UnrealEditor-UnrealAiEditor.dll`** is built for your engine + project.
3. Launch the editor; enable plugins if prompted (**Edit → Plugins → Unreal AI Editor**).

### Create an install zip

Zip the `Plugins/UnrealAiEditor` folder (omit `Binaries`, `Intermediate`, and `.git` from release copies). Example from repo root:

```powershell
New-Item -ItemType Directory -Force -Path dist | Out-Null
Compress-Archive -Path 'Plugins\UnrealAiEditor' -DestinationPath 'dist\UnrealAiEditor.zip' -Force
```

## Manual verification

1. **Unreal AI** toolbar button or **Window â†’ Unreal AI â†’ Agent Chat** opens without errors.
2. Type text in the composer, **Send** â€” assistant area fills with stub streamed text (or a real LLM response when API keys are configured).
3. **Window â†’ Unreal AI â†’ AI Settings** â†’ **Save** â€” confirm `%LOCALAPPDATA%\UnrealAiEditor\settings\plugin_settings.json` exists (Windows).
4. **Edit â†’ Project Settings â†’ Plugins â†’ Unreal AI Editor** â€” fields visible and save to config.

## Vector index (SQLite) troubleshooting

The optional **retrieval / vector** store lives under `%LOCALAPPDATA%\UnrealAiEditor\vector\<project_id>\` (`index.db` + `manifest.json`). If the editor log shows SQLite **disk I/O** or **failed to open** spam, or retrieval stays in **error**:

1. Close Unreal, delete the folder for that `project_id` (or only `index.db` + `manifest.json`) to reset the index.
2. In **AI Settings**, disable **auto index on project open** temporarily if the disk or AV is locking files.
3. After repeated failures the manifest may set **`vector_db_open_retry_not_before_utc`**: automatic rebuilds back off until that time (15 minutes by default after errors).

Background indexing runs on a **thread pool** and does not marshal bulk embedding HTTP through the game thread; interactive **Agent Chat** uses retrieval **prefetch** so context build avoids blocking the UI.

Index builds batch multiple chunk texts per **`/embeddings`** HTTPS request (see **AI Settings → Retrieval**, chunks per HTTP + delay); commits are **phased by priority** (project `Source/` first, then plugins, config/docs, content, virtual corpora) so high-priority chunks can appear in SQLite before the full run finishes; optional **P0 time budget (seconds)** forces earlier commits for completed P0 sources. Use batch size **1** if your embedding endpoint only accepts a single `input` string.

## Harness testing & agent handoff

**Single doc for prompts, catalog, dispatch, scripts, and escalation:** [`docs/tooling/AGENT_HARNESS_HANDOFF.md`](../../docs/tooling/AGENT_HARNESS_HANDOFF.md). Use it when onboarding a new agent or a fresh iteration thread. **Primary qualitative batches:** `.\tests\qualitative-tests\run-qualitative-headed.ps1` from repo root.

## Automation tests (Editor, dev builds)

When `WITH_DEV_AUTOMATION_TESTS` is enabled (default for **Development** Editor targets), the module registers Session Frontend tests:

| Test | What it guards |
|------|----------------|
| `UnrealAiEditor.Tools.JsonHelpers` | `UnrealAiToolJson::Ok` / `Error` round-trip (shared JSON contract for all tools). |
| `UnrealAiEditor.Tools.DispatchEditorSmoke` | Router returns structured `not_implemented` for unknown IDs; `editor_get_selection` reaches `GEditor` and returns `actor_paths` / `labels` / `count`. |
| `UnrealAiEditor.Tools.CatalogMatrix` | Every catalog tool ID: dispatch with `{}`; asserts non-empty, parseable JSON with `ok` / `status` / `error`; writes `Saved/UnrealAiEditor/Automation/tool_matrix_last.json`. |
| `UnrealAiEditor.Tools.ConsoleCommandAllowlist` | `console_command` unknown key fails; `r_vsync` without args fails; `stat_fps` maps to `stat fps`. |
| `UnrealAiEditor.Tools.RemovedCatalogToolsDispatchContract` | Catalog-removed tool ids (IR/T3D splits, legacy settings/viewport entry points, `agent_emit_todo_plan`) return `not_implemented` from the dispatcher. |
| `UnrealAiEditor.Retrieval.EmbeddingRequestDefaults` | Indexer sets `bBackgroundIndexer` only for rebuild embeds; default request stays interactive. |
| `UnrealAiEditor.Retrieval.OpenAiBatchEmbeddingsParse` | Multi-`data[]` embedding JSON is reordered by `index` correctly. |
| `UnrealAiEditor.Retrieval.VectorManifestCooldownRoundTrip` | Manifest JSON persists `vector_db_open_retry_not_before_utc`. |
| `UnrealAiEditor.Harness.*` | Plan DAG parse/validation / ready-set (see `UnrealAiAgentHarnessAutomationTests.cpp`). |

**Run from repo root (headless automation + matrix):**

```powershell
.\build-editor.ps1 -AutomationTests -Headless
```

Artifacts: `Saved/UnrealAiEditor/Automation/tool_matrix_last.json`, editor logs under `Saved/Logs/`. Primary qualitative batches: [`tests/qualitative-tests/run-qualitative-headed.ps1`](../tests/qualitative-tests/run-qualitative-headed.ps1). Maintainer entry point: [`docs/tooling/AGENT_HARNESS_HANDOFF.md`](../docs/tooling/AGENT_HARNESS_HANDOFF.md).

**Run in editor:** **Tools â†’ Session Frontend** (or **Test Automation** in some UE versions) â†’ search `UnrealAiEditor`, or console `UnrealAi.RunCatalogMatrix [filter]`.

Catalog matrix tests catch **contract** breaks (empty responses, non-JSON, missing structured fields). They do **not** prove every tool succeeds with minimal args (`bOk` may be false by design). Maps/assets/PIE still need targeted integration tests.

## Out of scope (product)

Hosted product backend, mandatory cloud accounts, separate **required** OS service for core chat (optional local helpers are fine). Features not yet implemented are reflected per tool (`not_implemented` or catalog notes), not listed here.

## Context service

Per-chat **context assembly** (`IAgentContextService` / `FUnrealAiContextService`): attachments, tool-result memory, editor snapshot, `BuildContextWindow` â†’ persisted as `chats/<project_id>/threads/<thread_id>/context.json`. See repo [`docs/context/context-management.md`](../../docs/context/context-management.md) and the **target** layered-ingestion plan [`docs/planning/context-ingestion-refactor.md`](../../docs/planning/context-ingestion-refactor.md).

## Engine module layout

- `Public/` â€” module API, tab IDs, `UUnrealAiEditorSettings`.
- `Private/` â€” Slate widgets/tabs (including **Debug**), style, commands, persistence, LLM HTTP transport, **`Private/Context/`** context service, **`Private/Tools/`** tool catalog load + modular dispatch (`UnrealAiToolDispatch.cpp` router, `UnrealAiToolJson.*`, `UnrealAiToolDispatch_*` including `_Search`, `_BlueprintTools`, `_GenericAssets`, â€¦), module implementation.
- `Resources/` â€” **`tools.main.json`** (primary catalog: `meta` + main roster); **`tools.blueprint.json`** / **`tools.environment.json`** (merged fragments).
