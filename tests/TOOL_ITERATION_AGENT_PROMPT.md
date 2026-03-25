**Read first:** [`docs/AGENT_HARNESS_HANDOFF.md`](../docs/AGENT_HARNESS_HANDOFF.md) — full harness context, scripts, escalation, and when to propose adding/removing tools. This file focuses on the **catalog matrix** iteration loop.

## Mission

1. **Server-side first:** Prefer fixing behavior in C++ dispatch (`UnrealAiToolDispatch*.cpp`) and clarifying **`UnrealAiToolCatalog.json`** parameter schemas and descriptions. Do **not** rely on changing only the chat system prompt as the primary fix.
2. **Normalize inputs:** Accept common aliases, fill in sensible defaults, return **clear JSON errors** with hints when input is invalid.
3. **Fixtures:** Add or update `tests/fixtures/<tool_id>.json` so the catalog matrix test exercises realistic arguments.
4. **Verify:** Rebuild and re-run the matrix; iterate until `summary.contract_violations` is zero and targeted manual scenarios behave.

## Inputs to read each iteration

| Artifact | Purpose |
|----------|---------|
| `tests/out/last-matrix.json` | Per-tool `bOk`, `parsed_status`, `tier`, `response_preview`, `contract_violations` |
| `tests/out/editor-last.log` | Automation failures, engine errors |
| `tests/fixtures/<tool_id>.json` | Optional args passed to dispatch for that tool in `CatalogMatrix` |
| `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json` | Tool definition, `parameters`, `summary` |
| Matching `UnrealAiToolDispatch_*.cpp` | Implementation of the tool |

Optional: run `python tests/summarize_tool_matrix.py` (or `python tests/summarize_tool_matrix.py --matrix path`) for a short stdout summary.

## Standard loop

1. Identify failing tools from `contract_violations` or rows with `tier=contract_fail`.
2. Reproduce the failure: read the row’s `error_message` / `response_preview` and the tool’s catalog entry.
3. Implement normalization or clearer validation in dispatch; adjust catalog JSON Schema (`parameters`) so the model gets better guidance.
4. Add or extend `tests/fixtures/<tool_id>.json` if the default `{}` case is not representative.
5. Build: `.\build-editor.ps1 -Headless`  
   If the plugin DLL is locked: `.\build-editor.ps1 -Restart -Headless`
6. Run tests (the script **prints a heartbeat every 30s** — long silence is normal; matrix runs are slow):  
   - Matrix-focused: `.\tests\run-unreal-ai-tests.ps1 -CatalogMatrixOnly -Summarize`
   - Headed (visible editor): `.\tests\run-unreal-ai-tests.ps1 -Headed`
   - Full plugin tests: `.\tests\run-unreal-ai-tests.ps1` (default filter runs every automation test)
   - With summary: add `-Summarize`  
   Chromium / CEF lines in the console are usually harmless.
7. Re-read `tests/out/last-matrix.json` and repeat until clean.

## Reading logs and errors (verbose output)

`run-unreal-ai-tests.ps1` already passes **verbose automation logging** to the editor:

- `-LogCmds=LogAutomationController Verbose, LogAutomationCommandLine Verbose`

After each run, the script copies the **newest** `Saved/Logs/*.log` file to **`tests/out/editor-last.log`** and prints **`ProcessExitCode`**, **`Result=Fail` hits**, **`Matrix summary.contract_violations`**, and **`FinalExitCode`**. Treat **`FinalExitCode: 0`** (green) as success for the full gate.

**You should actively scan `tests/out/editor-last.log` for problems**, not only the matrix JSON:

| Signal | What to do |
|--------|------------|
| `Error:` / `Fatal:` / `ensure` | Investigate; often compile/runtime issues or failed automation setup |
| `Result=Fail` / `Failed Test Count` | Which automation test failed; scroll nearby lines for the reason |
| `LogAutomationController` / `LogAutomationCommandLine` | Per-test progress (enabled by the flags above) |
| Plugin DLL lock / crash | Close any stray `UnrealEditor.exe`, then `.\build-editor.ps1 -Restart -Headless` |

**Caveat:** The log file is the **most recently modified** `.log` under `Saved/Logs`. If another process wrote a newer log, `editor-last.log` might not be the matrix run—avoid parallel editors when capturing.

**Strict matrix gate:** Use `.\tests\run-unreal-ai-tests.ps1 -Headed -Summarize` so Python prints a short matrix summary; `python tests\summarize_tool_matrix.py --strict` fails if any tool row is `bOk=false`, not only contract violations.

## Commands (cheat sheet)

```powershell
# Typical efficient batch: edit many files → build once → test once
.\build-editor.ps1 -Headless
# If UnrealEditor.exe has the DLL locked:
# .\build-editor.ps1 -Restart -Headless

.\tests\run-unreal-ai-tests.ps1 -Headed -Summarize

# JSON-only changes (catalog/fixtures): skip build, still run tests
.\tests\run-unreal-ai-tests.ps1 -Headed -Summarize

python tests\summarize_tool_matrix.py
python tests\summarize_tool_matrix.py --strict
```

**Filter** only tools whose id contains a substring (faster iteration):

```powershell
.\tests\run-unreal-ai-tests.ps1 -Headed -MatrixFilter actor
```

**In-editor without relaunching the EXE:** open **Window → Developer Tools → Output Log**, run:

```
UnrealAi.RunCatalogMatrix
```

Optional filter:

```
UnrealAi.RunCatalogMatrix blueprint
```

Then copy `Saved/UnrealAiEditor/Automation/tool_matrix_last.json` or compare with `tests/out/last-matrix.json` after a scripted run.

**Static + LLM routing only** (does not execute tools in Unreal):

```powershell
.\tests\test-tools.ps1
.\tests\test-tools.ps1 -Llm -LlmMax 10
```

## Non-goals

- Do not “fix” reliability solely by making the user prompt longer.
- Do not raise LLM round limits as a substitute for fixing ambiguous tool contracts.

## v1 reality (no safety enforcement)

This plugin’s **v1** does **not** implement a product-level safety sandbox: the agent can invoke tools that delete assets, actors, levels, or otherwise change the project. Catalog fields like `permission` or `side_effects` are **documentation hints**, not hard gates enforced by the editor for this iteration. When hardening tools, focus on **correctness, clear errors, and predictable JSON**—do not assume you must preserve or strengthen “safety” behavior that the runtime does not actually enforce.

## Notes on LLM round limits

If the agent hits **max tool/LLM rounds**, fix **tool UX** (defaults, error messages, single-call workflows) and/or raise **Max agent LLM rounds** in AI Settings. That is separate from matrix **contract** failures.
