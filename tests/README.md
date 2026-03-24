# Unreal AI Editor — automated tests

This folder holds **fixtures** and **captured output** for plugin automation. C++ tests live in the `UnrealAiEditor` module (`Private/Tests/`, `*AutomationTests.cpp`).

## Run from the repo root (recommended)

```powershell
.\run-unreal-ai-tests.ps1
```

Optional:

- **Build first:** `.\run-unreal-ai-tests.ps1 -Build`
- **Engine path:** `$env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'` or `.\run-unreal-ai-tests.ps1 -EngineRoot 'D:\Epic\UE_5.7'`
- **Automation filter:** `.\run-unreal-ai-tests.ps1 -TestFilter UnrealAiEditor` (default)
- **Only matrix tools matching a substring:** `.\run-unreal-ai-tests.ps1 -MatrixFilter blueprint` (passes `-UnrealAiToolMatrixFilter=blueprint` to the editor)

Close **Unreal Editor** before building if the plugin DLL is locked.

## Outputs (LLM-oriented)

After a run:

| File | Description |
|------|-------------|
| [`out/last-matrix.json`](out/last-matrix.json) | Copy of `Saved/.../tool_matrix_last.json` — per-tool `bOk`, `parsed_status`, response preview, contract violations |
| [`out/editor-last.log`](out/editor-last.log) | Latest editor log from `Saved/Logs` (newest `.log`) |

Interpret **`contract_violations`**: empty body, unparseable JSON, or missing structured fields — these are bugs to fix. Rows where `bOk` is false with valid JSON are often **expected** for `{}` arguments; use fixtures under `fixtures/` for deeper checks later.

## Fixtures (optional)

Place `tests/fixtures/<tool_id>.json` with a JSON object of arguments. The catalog matrix test loads it when present instead of `{}`.

## Session Frontend

You can still run tests from the editor: **Tools → Session Frontend** (or **Test Automation**) → filter `UnrealAiEditor`.
