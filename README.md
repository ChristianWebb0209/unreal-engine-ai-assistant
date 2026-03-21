# Dungeon Architect

Unreal Engine **editor AI assistant** (see **`PRD.md`**).

## Repository layout

- **Unreal project (blank) at repo root:** `blank.uproject`, `Config/`, `Content/`, and **`Plugins/UnrealAiEditor/`** — open `blank.uproject` to develop and test the plugin.
- **Rename folder to `ue-plugin`:** close Cursor/IDE, then run `scripts/Rename-Root-To-UePlugin.ps1` (or manually rename `dungeon-architect` → `ue-plugin` under `C:\Github`).

## MVP architecture (summary)

- **No server and no product backend** — everything ships in the **Unreal editor plugin** (UI, tools, persistence, orchestration).
- **Network:** user-configured **HTTPS to third-party LLM APIs** only (e.g. OpenRouter, Anthropic, OpenAI). Optional **localhost** (e.g. MCP) runs **inside the editor**, not a remote product API.
- **No vector / semantic index in v1** — context via tools, Asset Registry, and deterministic search; see **`agent-and-tool-requirements.md`**.

Canonical docs:

| Document | Purpose |
|----------|---------|
| [`PRD.md`](PRD.md) | Product requirements, local persistence, MVP deployment |
| [`agent-and-tool-requirements.md`](agent-and-tool-requirements.md) | Modes (Ask / Fast / Agent), Level B subagents, tools, indexing scope |
| [`docs/README.md`](docs/README.md) | What lives under `docs/` |
| [`docs/context-service.md`](docs/context-service.md) | Per-chat context assembly, `context.json`, budgets |
