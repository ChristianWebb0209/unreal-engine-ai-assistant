# Dungeon Architect

Unreal Engine **editor AI assistant** (see **`PRD.md`**).

## Build and run (Windows, UE 5.7)

1. **Prerequisites:** Visual Studio 2022 **Desktop development with C++** (or Build Tools + MSVC), and the [.NET Framework 4.8 Developer Pack](https://learn.microsoft.com/en-us/dotnet/framework/install/guide-for-developers) (needed for Unreal Build Tool / Swarm on some machines).
2. **Generate project files** (after cloning or changing targets):  
   `"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="%CD%\blank.uproject" -game -rocket -progress`
3. **Compile the Editor** (builds the `Blank` game module and the **UnrealAiEditor** plugin):  
   `"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" BlankEditor Win64 Development -project="%CD%\blank.uproject" -waitmutex`
4. **Launch Unreal Editor** (adjust the engine path if your install differs):  
   `"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "%CD%\blank.uproject"`

From **Cursor**, use the integrated terminal for steps 2–4, or define a [task](https://code.visualstudio.com/docs/editor/tasks) that runs the same commands.

The repo includes a minimal **`Source/Blank`** runtime module so Unreal Build Tool can compile the C++ plugin (the project previously had only plugin sources and no `Source/` folder).

## Repository layout

- **Unreal project (blank) at repo root:** `blank.uproject`, `Config/`, `Content/`, **`Source/Blank/`**, and **`Plugins/UnrealAiEditor/`** — open `blank.uproject` to develop and test the plugin.
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
