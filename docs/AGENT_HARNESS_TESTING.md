# Agent harness testing (LLM + editor)

**Start here for the full picture (prompts, catalog, dispatch, escalation): [AGENT_HARNESS_HANDOFF.md](./AGENT_HARNESS_HANDOFF.md)** — single handoff file for new agents continuing harness work.

This document describes how to run the **same agent loop as Agent Chat** from the **Output Log console** or automation, capture **structured artifacts**, and use a **deterministic LLM fixture** for CI.

## Artifacts

After a run, look under:

`Saved/UnrealAiEditor/HarnessRuns/<timestamp>/`

| File | Purpose |
|------|---------|
| `run.jsonl` | One JSON object per line: `run_started`, `assistant_delta`, `tool_start` / `tool_finish`, `continuation`, `todo_plan`, `run_finished` |
| `context_window_*.txt` | Optional dumps of `BuildContextWindow` (when enabled on the file sink) |

Thread context and conversation still persist under the plugin data root as usual (`*-context.json`), same as interactive chat.

## Console: `UnrealAi.RunAgentTurn`

From **Window → Developer Tools → Output Log**, run:

```
UnrealAi.RunAgentTurn <MessageFilePath> [ThreadGuid] [agent|ask|orchestrate] [OutputDir]
```

- **MessageFilePath** — UTF-8 text file containing the user message (same as typing in chat).
- **ThreadGuid** — Optional. Omit to start a **new** thread id (new `FGuid`). Reuse a guid string to continue the same conversation/context as an existing thread.
- **Mode** — Default `agent`. Matches Ask / Agent / Orchestrate in the chat UI.
- **OutputDir** — Optional. If omitted, a timestamped folder is created under `Saved/UnrealAiEditor/HarnessRuns/`.

The command must execute on the **game thread** (normal editor console).

## Deterministic LLM (fixture transport)

Set the environment variable **`UNREAL_AI_LLM_FIXTURE`** to a JSON file **before** starting the editor. The plugin will use that file instead of HTTP or the offline stub, until the variable is cleared.

Example (PowerShell):

```powershell
$env:UNREAL_AI_LLM_FIXTURE = "C:\Github\ue-plugin\tests\harness_llm_fixture.example.json"
```

Relative paths are resolved against the **project directory** (`FPaths::ProjectDir()`).

Schema: [tests/harness_llm_fixture.schema.json](../tests/harness_llm_fixture.schema.json). Example: [tests/harness_llm_fixture.example.json](../tests/harness_llm_fixture.example.json).

Each `StreamChatCompletion` call from the harness consumes the **next** object in `responses[]`. Each response is a list of stream `events`: `assistant_delta`, `thinking_delta`, `tool_calls`, `finish`, `error`. Match the number of `responses` to the number of LLM rounds your scenario needs (one user message can trigger many rounds if tools run).

After changing the env var, **reload LLM config** from AI Settings (or restart the editor) so `FUnrealAiBackendRegistry` rebuilds the harness with the fixture transport.

## Real LLM (OpenAI-compatible API)

Configure API keys in **AI Settings** as usual. Do **not** set `UNREAL_AI_LLM_FIXTURE`. The HTTP transport is used when a key is present.

## Python: assert `run.jsonl`

```powershell
python tests\assert_harness_run.py Saved\UnrealAiEditor\HarnessRuns\20260101_120000\run.jsonl --expect-tool editor_get_selection
```

Use `--require-success` if the harness must finish without error.

## Automation / ExecCmds

You can drive a run from command-line automation, for example:

```
-UnrealAiToolMatrixFilter=...
```

For a full agent turn, use `-ExecCmds="UnrealAi.RunAgentTurn C:/path/to/msg.txt; Quit"` (ensure paths and quoting work on your shell). Prefer a **headed** or **Cmd** session with the project loaded; cold start can take minutes.

## Relation to `UnrealAi.RunCatalogMatrix`

| Command | What it exercises |
|---------|---------------------|
| `UnrealAi.RunCatalogMatrix` | **Tool dispatch only** — no LLM |
| `UnrealAi.RunAgentTurn` | **Full harness** — LLM transport + tools + context + persistence |

## Catalog alignment

New tools in `UnrealAiToolCatalog.json` must have matching **C++ dispatch** and, for matrix coverage, optional **`tests/fixtures/<tool_id>.json`**. Example: `blueprint_format_graph` uses [tests/fixtures/blueprint_format_graph.json](../tests/fixtures/blueprint_format_graph.json).
