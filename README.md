# Unreal AI Editor

**Unreal AI Editor** is a **free, open-source** Unreal Engine plugin that brings a full **agentic AI assistant** into the editor: streaming chat, a large **tool catalog** for assets and levels, Blueprint graph authoring via a compact **IR format**, local persistence, and bring-your-own API keys (OpenRouter, Anthropic, OpenAI, and similar). You pay **only** for whichever LLM provider you choose—**no** subscription to us, **no** lock-in, and **no** separate product server. The goal is **feature depth comparable to commercial Unreal AI assistants** that often sell for **$100+** on marketplaces—while staying **local-first**, auditable, and under your control.

See **`PRD.md`** for product requirements and **`Plugins/UnrealAiEditor/README.md`** for plugin-specific implementation notes.

## Build and run (Windows, UE 5.7)

1. **Prerequisites:** Visual Studio 2022 **Desktop development with C++** (or Build Tools + MSVC), and the [.NET Framework 4.8 Developer Pack](https://learn.microsoft.com/en-us/dotnet/framework/install/guide-for-developers) (needed for Unreal Build Tool on some machines).
2. From the repo root, use the helper script (set `UE_ENGINE_ROOT` if your engine is not at the default path):

   ```powershell
   .\build-blank-editor.ps1 -Headless
   ```

   Or with options:

   - **Generate project files:** `.\build-blank-editor.ps1 -GenerateProjectFiles`
   - **Custom engine:** `$env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'; .\build-blank-editor.ps1 -Headless`

3. **Launch Unreal Editor** (after a successful build):

   `"<Engine>\Engine\Binaries\Win64\UnrealEditor.exe" "%CD%\blank.uproject"`

From **Cursor** or VS Code, use the integrated terminal for the same commands.

The repo includes a minimal **`Source/Blank`** runtime module so Unreal Build Tool can compile the C++ plugin alongside the blank game target.

## What you get (high level)

- **In-editor UI:** **Window → Unreal AI** and **Tools → Unreal AI** (Agent Chat, AI Settings, Quick Start, Help, **Debug**). Toolbar button and **Ctrl+K** for Agent Chat.
- **Agent Chat:** Streaming replies, tool call cards, todo plans, context attachments, per-thread persistence under `%LOCALAPPDATA%\UnrealAiEditor\` (Windows).
- **Tools:** Broad catalog in `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`—scene/asset/source search, editor snapshots, Blueprint **compile** / **export IR** / **apply IR**, generic **asset** create/export/apply properties, packaging helpers, and more (see catalog and dispatch modules under `Plugins/UnrealAiEditor/Source/.../Tools/`).
- **Settings:** API keys, models, usage stats, presets—persisted locally; optional OpenRouter and other providers.
- **No product backend:** networking is **only** to LLM APIs you configure (plus optional local tooling such as MCP on localhost, if you wire it).

## Repository layout

- **Unreal project at repo root:** `blank.uproject`, `Config/`, `Content/`, **`Source/Blank/`**, **`Plugins/UnrealAiEditor/`** — open `blank.uproject` to develop and test the plugin.

## MVP architecture (summary)

- **No server and no product backend** — core functionality ships in the **Unreal editor plugin** (UI, tools, persistence, orchestration).
- **Network:** user-configured **HTTPS to third-party LLM APIs** only (e.g. OpenRouter, Anthropic, OpenAI). Optional **localhost** tooling (e.g. MCP) runs **inside or beside the editor**, not a remote product API.
- **No vector / semantic index in v1** — context via tools, Asset Registry, and deterministic search; see **`agent-and-tool-requirements.md`**.

## Competitive positioning

For a structured comparison of depth vs. commercial “deep integration” assistants, see **`COMPETITION-GAP.md`**.

Canonical docs:

| Document | Purpose |
|----------|---------|
| [`PRD.md`](PRD.md) | Product requirements, local persistence, MVP deployment |
| [`agent-and-tool-requirements.md`](agent-and-tool-requirements.md) | Modes (Ask / Fast / Agent), Level B subagents, tools, indexing scope |
| [`COMPETITION-GAP.md`](COMPETITION-GAP.md) | Parity gaps and differentiators vs. paid assistants |
| [`docs/context-management.md`](docs/context-management.md) | Per-chat context assembly, `context.json`, budgets, planning artifacts |
