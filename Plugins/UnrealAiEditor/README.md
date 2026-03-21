# Unreal AI Editor (Editor Plugin Shell)

UI shell + **in-editor** stub implementations (persistence, fake stream) for an Unreal Engine 5.5+ **Editor** plugin. **MVP:** no product server or hosted backend — all logic lives in the plugin; see [`PRD.md`](../../PRD.md) §2.3–§2.5.

## What is implemented

- **Window → Unreal AI** submenu: Agent Chat, AI Settings, API Keys & Models, Quick Start, Help.
- **Nomad tabs** for each surface; dockable like other editor tabs.
- **Agent Chat**: transcript (`FUnrealAiChatTranscript`), tool rows (`SToolCallCard`), thinking lane (`SThinkingBlock`), assistant streaming + optional typewriter (`SAssistantStreamBlock`), todo panel (`STodoPlanPanel`), **Stop** → `CancelTurn`. Streaming defaults on (`UUnrealAiEditorSettings::bStreamLlmChat`). See [`docs/chat-renderer.md`](../../docs/chat-renderer.md).
- **API Keys & Models**: provider/base URL/key/model fields, mask toggle, **Test connection** (stub), **Save** → writes JSON via persistence stub.
- **AI Settings**: placeholder copy + **Save** → `plugin_settings.json` under local data root.
- **Project Settings → Plugins → Unreal AI Editor**: `UUnrealAiEditorSettings` (default agent, auto connect, verbose logging, OpenRouter fields).
- **In-module stubs** (`Private/Backend/` — not a separate server process): persistence (writes under `%LOCALAPPDATA%\UnrealAiEditor\` on Windows), chat service (fake stream), model connector (delayed success).
- **Tool catalog + execution:** [`Resources/UnrealAiToolCatalog.json`](Resources/UnrealAiToolCatalog.json) — single JSON (`meta` + `tools[]`). **`FUnrealAiToolExecutionHost`** (`Private/Tools/`) implements `IToolExecutionHost::InvokeTool` and dispatches to UE5 handlers in `UnrealAiToolDispatch.cpp` + `UnrealAiToolDispatch_Search.cpp` (game thread). **Fuzzy search:** `scene_fuzzy_search` (actors in level), `asset_index_fuzzy_search` (Asset Registry index), `source_search_symbol` (project Source/Config/Plugins/*/Source files); shared scoring in `UnrealAiFuzzySearch.cpp`. Other tool IDs return a structured `not_implemented` until handlers are added.

## Install into a UE project

**This repo** already has a minimal UE project at the root (`blank.uproject`) with this plugin under `Plugins/UnrealAiEditor/`. Open that `.uproject` to build and run.

**Another project:** copy the `UnrealAiEditor` plugin folder to `<YourProject>/Plugins/UnrealAiEditor/`, then:

1. Right-click the `.uproject` → **Generate Visual Studio project files** (Windows) or equivalent.
2. Build the **Editor** target.
3. Launch the editor; enable the plugin if prompted (**Edit → Plugins → Unreal AI Editor**).

## Manual verification

1. **Window → Unreal AI → Agent Chat** opens without errors.
2. Click **Connect**, type text in the composer, **Send** — assistant area fills with stub streamed text.
3. **Window → Unreal AI → API Keys & Models** → **Save** — confirm `%LOCALAPPDATA%\UnrealAiEditor\settings\plugin_settings.json` exists (Windows).
4. **Edit → Project Settings → Plugins → Unreal AI Editor** — fields visible and save to config.

## Automation tests (Editor, dev builds)

When `WITH_DEV_AUTOMATION_TESTS` is enabled (default for **Development** Editor targets), the module registers Session Frontend tests:

| Test | What it guards |
|------|----------------|
| `UnrealAiEditor.Tools.JsonHelpers` | `UnrealAiToolJson::Ok` / `Error` round-trip (shared JSON contract for all tools). |
| `UnrealAiEditor.Tools.DispatchEditorSmoke` | Router returns structured `not_implemented` for unknown IDs; `editor_get_selection` reaches `GEditor` and returns `actor_paths` / `labels` / `count`. |

**Run:** Editor → **Tools → Session Frontend** (or **Test Automation** in some UE versions) → search `UnrealAiEditor.Tools`.

These are **smoke** tests: they catch broken wiring and JSON shape regressions; they do **not** replace per-tool integration tests (maps, assets, PIE) for every tool ID.

## Out of scope (shell)

Real HTTP/OpenRouter, tool execution, `@` asset search, encryption beyond future work, separate OS service process.

## Context service

Per-chat **context assembly** (`IAgentContextService` / `FUnrealAiContextService`): attachments, tool-result memory, editor snapshot, `BuildContextWindow` → persisted as `chats/<project_id>/threads/<thread_id>/context.json`. See repo [`docs/context-service.md`](../../docs/context-service.md).

## Engine module layout

- `Public/` — module API, tab IDs, `UUnrealAiEditorSettings`.
- `Private/` — Slate widgets/tabs, style, commands, persistence/stream stubs, **`Private/Context/`** context service, **`Private/Tools/`** tool catalog load + modular dispatch (`UnrealAiToolDispatch.cpp` router, `UnrealAiToolJson.*`, `UnrealAiToolDispatch_Context`, `_Actors`, `_EditorUi`, `_Viewport`, `_ProjectFiles`, `_Console`, `_AssetsMaterials`, `_MoreAssets`, `_BlueprintTools`, `_EditorMore`, `_BuildPackaging`, `_ExtraFeatures`, `_ContentBrowserEx`, `_Pie`, `_Misc`, `_Search`), module implementation.
- `Resources/` — **`UnrealAiToolCatalog.json`** (single tool catalog for the plugin).
