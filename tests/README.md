# Unreal AI Editor — automated tests

This folder holds **fixtures** and **captured output** for plugin automation. C++ tests live in the `UnrealAiEditor` module (`Private/Tests/`, `*AutomationTests.cpp`).

## Run from the repo root (recommended)

```powershell
.\run-unreal-ai-tests.ps1
```

**It is probably not stuck.** The script waits for Unreal to exit and prints a heartbeat every 30s. The default `UnrealAiEditor` filter runs **all** plugin automation tests (slow). The matrix test alone invokes **every** catalog tool and can take **10–30+ minutes**. Random `Chromium` / `google_update` lines are normal noise.

Optional:

- **Build first:** `.\run-unreal-ai-tests.ps1 -Build`
- **Engine path:** `$env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'` or `.\run-unreal-ai-tests.ps1 -EngineRoot 'D:\Epic\UE_5.7'`
- **Matrix only (skip harness / other tests):** `.\run-unreal-ai-tests.ps1 -CatalogMatrixOnly` — equivalent to `-TestFilter UnrealAiEditor.Tools.CatalogMatrix` (still slow because every tool runs).
- **Automation filter:** `.\run-unreal-ai-tests.ps1 -TestFilter UnrealAiEditor` (default)
- **Only matrix tools matching a substring:** `.\run-unreal-ai-tests.ps1 -MatrixFilter blueprint` (passes `-UnrealAiToolMatrixFilter=blueprint` to the editor)
- **Headed editor (visible window):** `.\run-unreal-ai-tests.ps1 -Headed` — uses `UnrealEditor.exe` instead of `UnrealEditor-Cmd.exe`; same automation (`ExecCmds` … `Quit`).
- **Allow blocking dialogs:** `.\run-unreal-ai-tests.ps1 -AllowDialogs` — omits `-unattended` (can hang if a dialog waits; useful for debugging).
- **Human/LLM-friendly matrix summary:** `.\run-unreal-ai-tests.ps1 -Summarize` — runs `tests/summarize_tool_matrix.py` on `tests/out/last-matrix.json`.

Close **Unreal Editor** before `-Build` if the plugin DLL is locked.

### LLM iteration prompt

For a documented loop (read matrix → patch dispatch/catalog → rebuild → re-run), see **[TOOL_ITERATION_AGENT_PROMPT.md](TOOL_ITERATION_AGENT_PROMPT.md)**. That file includes **copy-paste agent instructions**: headed runs, **batching edits before one build**, when rebuild is required vs JSON-only re-run, and **how to read `editor-last.log` for errors** alongside the matrix JSON.

### Summarize matrix JSON only

```powershell
python tests\summarize_tool_matrix.py
python tests\summarize_tool_matrix.py --matrix tests\out\last-matrix.json
python tests\summarize_tool_matrix.py --strict
```

`--strict` also fails the script exit code when any non-skipped row has `bOk=false`.

## Outputs (LLM-oriented)

After a run:

| File | Description |
|------|-------------|
| [`out/last-matrix.json`](out/last-matrix.json) | Copy of `Saved/.../tool_matrix_last.json` — per-tool `bOk`, `parsed_status`, response preview, contract violations |
| [`out/editor-last.log`](out/editor-last.log) | Latest editor log from `Saved/Logs` (newest `.log`) |

Interpret **`contract_violations`**: empty body, unparseable JSON, or missing structured fields — these are bugs to fix. Rows where `bOk` is false with valid JSON are often **expected** for `{}` arguments; use fixtures under `fixtures/` for deeper checks later.

## In-editor matrix (no script)

With the editor already open: **Output Log** console command:

```
UnrealAi.RunCatalogMatrix
```

Optional filter substring: `UnrealAi.RunCatalogMatrix your_substring`

Writes `Saved/UnrealAiEditor/Automation/tool_matrix_last.json`. Use **Session Frontend** if you prefer the UI: **Tools → Session Frontend** → filter `UnrealAiEditor` → run `UnrealAiEditor.Tools.CatalogMatrix`.

## Fixtures (optional)

Place `tests/fixtures/<tool_id>.json` with a JSON object of arguments. The catalog matrix test loads it when present instead of `{}`.

## Tool catalog routing (Python)

```powershell
.\test-tools.ps1
```

Uses `tests/tool_catalog_routing_check.py` (static routing; optional `-Llm` for API tool-name checks). For Unreal automation instead:

```powershell
.\test-tools.ps1 -UseUnrealAutomation
.\test-tools.ps1 -UseUnrealAutomation -Headed
```
