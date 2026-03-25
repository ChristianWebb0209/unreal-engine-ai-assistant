# Unreal AI Editor

**Unreal AI Editor** is a **free, open-source** Unreal Engine plugin that brings a full **agentic AI assistant** into the editor: streaming chat, a large **tool catalog** for assets and levels, Blueprint graph authoring via a compact **IR format**, local persistence, and bring-your-own API keys (OpenRouter, Anthropic, OpenAI, and similar). You pay **only** for whichever LLM provider you choose—**no** subscription to us, **no** lock-in, and **no** separate product server. The goal is **feature depth comparable to commercial Unreal AI assistants** that often sell for **$100+** on marketplaces—while staying **local-first**, auditable, and under your control.

## Competitive analysis

On **[Fab](https://www.fab.com/channels/unreal-engine)** and the broader Unreal ecosystem, most “AI in the editor” products follow the same economics: you buy a **plugin license** (often about **$50–$150** one-time for serious copilots—**check each listing**) and you still use **bring-your-own API keys** for OpenAI, Anthropic, Google, local Ollama, etc. In other words, **you pay the marketplace for the integration**, then **you pay the model vendor for tokens**—same pattern as this project, except **here the plugin itself is $0**.

| | **Unreal AI Editor** (this repo) | Paid Fab / marketplace peers (examples) |
|---|-----------------------------------|----------------------------------------|
| **Plugin license** | **Free** (open source) | Typically **paid**; many copilots land around **~$100** one-time (varies by title, sale, region—verify on Fab). |
| **LLM / inference cost** | **BYOK** (and/or local endpoints you configure) | Essentially always **BYOK** or local models—you still fund API usage yourself. |
| **In-editor assistant** | Agent **chat**, streaming, attachments | Comparable products advertise **chat**, prompts from the editor, assistants. |
| **Tooling & workflows** | Large **tool catalog**, Blueprint **IR** apply/export, persistence | Peers emphasize **Blueprint generation**, **MCP/tools**, **multi-asset** workflows, etc.—same problem space, different implementation/breadth. |
| **Lock-in** | **No** product backend; code you can audit | Depends on vendor; usually **no** token markup, but **yes** license fee for the plugin. |

**Examples on Fab** (illustrative, not exhaustive—search Fab for “AI”, “Blueprint”, “GPT”, “assistant”): [Blueprint Generator AI (Kibibyte Labs)](https://www.fab.com/listings/6aa00d98-0db0-4f13-8950-e21f0a0eda2c), [AI Integration Toolkit (AIITK)](https://www.fab.com/listings/9de23ba0-e210-402d-9d76-441904e46f47), [HttpGPT](https://www.fab.com/listings/3edf406f-6a87-4f2f-bfdb-b0039f285541), [Offline AI Assistant](https://www.fab.com/listings/191b86f7-650d-4af8-a6dc-88f753e05968), [Universal Offline LLM](https://www.fab.com/listings/c5981158-7add-4977-9e08-440831058e5d). [UnrealAI](https://unrealai.studio/) (also positions as Fab-distributed) markets the same **BYOK / one-time plugin** idea with optional paid cloud credits—same dual cost structure minus **our $0** license.

For **tooling breadth** and known gaps, see [`docs/tool-goals.md`](docs/tool-goals.md) and [`docs/asset-type-coverage-matrix.md`](docs/asset-type-coverage-matrix.md). Narrative tool list: [`docs/tool-registry.md`](docs/tool-registry.md).

**Plugin implementation** (what ships today): [`Plugins/UnrealAiEditor/README.md`](Plugins/UnrealAiEditor/README.md). **Docs index:** [`docs/README.md`](docs/README.md).

## Build and run (Windows, UE 5.7)

1. **Prerequisites:** Visual Studio 2022 **Desktop development with C++** (or Build Tools + MSVC), and the [.NET Framework 4.8 Developer Pack](https://learn.microsoft.com/en-us/dotnet/framework/install/guide-for-developers) (needed for Unreal Build Tool on some machines).
2. From the repo root, use the helper script. **Optional:** copy `.env.example` to `.env` and set `UE_ENGINE_ROOT`, `OPENAI_*`, and harness timeouts; `build-editor.ps1` and harness scripts load `.env` before running. After editing `OPENAI_*`, run `.\scripts\sync-unreal-ai-from-dotenv.ps1` to refresh `%LOCALAPPDATA%\UnrealAiEditor\settings\plugin_settings.json`.

   ```powershell
   .\build-editor.ps1 -Headless
   ```

   Or with options:

   - **Generate project files:** `.\build-editor.ps1 -GenerateProjectFiles`
   - **Custom engine:** `$env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'; .\build-editor.ps1 -Headless`
   - **Clean rebuild (close editor first):** `.\build-editor.ps1 -Restart -Headless`

3. **Launch Unreal Editor** (after a successful build):

   `"<Engine>\Engine\Binaries\Win64\UnrealEditor.exe" "%CD%\blank.uproject"`

From **Cursor** or VS Code, use the integrated terminal for the same commands.

**Agent / harness iteration (prompts, tools, tests):** read [`docs/AGENT_HARNESS_HANDOFF.md`](docs/AGENT_HARNESS_HANDOFF.md) — one file for scripts, file map, and when to report bigger issues or tool catalog changes.

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
- **No vector / semantic index in v1** — context via tools, Asset Registry, and deterministic search; see [`docs/tool-goals.md`](docs/tool-goals.md).

## Maintainer docs

**Automated tests, harness scripts, and `tests/` scenarios** exist to **tune** prompts, catalog, and dispatch—they are **not** a supported end-user workflow. See [`tests/README.md`](tests/README.md).

## Canonical docs (in repo)

| Document | Purpose |
|----------|---------|
| [`docs/README.md`](docs/README.md) | Index of all `docs/` files |
| [`docs/AGENT_HARNESS_HANDOFF.md`](docs/AGENT_HARNESS_HANDOFF.md) | Harness iteration, scripts, tiers, escalation |
| [`docs/context-management.md`](docs/context-management.md) | Per-chat context assembly, `context.json`, budgets, planning artifacts |
| [`Plugins/UnrealAiEditor/README.md`](Plugins/UnrealAiEditor/README.md) | Plugin features and layout |
