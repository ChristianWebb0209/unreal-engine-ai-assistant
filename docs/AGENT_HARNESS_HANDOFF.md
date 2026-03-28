# Agent harness: testing & iteration (single entry point)

Give this file to any new agent or contributor. It is the single entry point for continuing work on the Unreal AI Editor testing harness, prompts, tool catalog, and dispatch.

Index of all `docs/` files: [docs/README.md](./README.md)

---
## What you are validating

| Layer | What “good” looks like |
|--------|-------------------------|
| Prompts (`prompts/chunks/*.md`) | System/developer instructions match real tool behavior, naming, and safety. Chunks are assembled in `UnrealAiPromptBuilder.cpp`. |
| Tool catalog (`Resources/UnrealAiToolCatalog.json`) | Each `tool_id` has accurate `summary`, JSON Schema `parameters`, and `modes` flags. |
| Dispatch (`UnrealAiToolDispatch*.cpp`) | Every catalog tool either runs, returns structured `not_implemented`, or validates input with clear JSON errors. |
| Harness (chat loop + persistence) | LLM transport → tool execution → persistence all connect; artifacts land under `Saved/UnrealAiEditor/HarnessRuns/<timestamp>/`. |

---
## Iteration order

1. Prompts (when the model skips tools, wrong-order tools, or ignores merge/layout contracts)
2. Catalog text + schema (when routing or parameter validation fails)
3. Dispatch validation + threading (when behavior differs from the catalog)
4. Optional per-tool JSON args under `tests/fixtures/<tool_id>.json` for catalog matrix (dispatch checks; no LLM)

---
## Three harness tiers

### 1a — CI / contract (headless)

`.\build-editor.ps1 -AutomationTests -Headless -SkipBlueprintFormatterSync`

### 2 — Live headed qualitative (real API)

Headed, qualitative runs using real API credentials from Unreal AI Editor settings.

- Run: `.\scripts\run-headed-live-scenarios.ps1 -MaxScenarios <N>`
- Optional context dumps: `UNREAL_AI_HARNESS_DUMP_CONTEXT=1`

Artifacts:
- Harness output: `Saved/UnrealAiEditor/HarnessRuns/<timestamp>/run.jsonl`
- Stable review copies: `tests/out/live_runs/<suite_id>/<scenario_id>/`

Key enforcement telemetry now emitted in `run.jsonl`:
- `enforcement_event` (action/mutation policy nudges/outcomes)
- `enforcement_summary` (aggregate counts for action intent, tool-backed action, explicit blockers, and mutation read-only nudges)
- Stream-first tool execution lifecycle event types:
  - `stream_tool_ready`
  - `stream_tool_exec_start`
  - `stream_tool_exec_done`
  - `stream_tool_call_incomplete_timeout`

Bundle review (example):
`python tests\bundle_live_harness_review.py tests\out\live_runs\tool_goals`

### 2b — Context workflows (multi-turn, single thread)

Multi-step workflows focused on how the context manager builds what enters/leaves the built context window.

- Run: `.\scripts\run-headed-context-workflows.ps1 -SuiteManifest tests\context_workflows\suite.json -DumpContext`
- Enable verbose context build tracing with `UNREAL_AI_CONTEXT_VERBOSE=1` (writes `context_build_trace_*.txt` per step).

Bundle diff review:
`python tests\bundle_context_workflow_review.py tests\out\context_runs\<suite_id>\<workflow_id>`

Snapshot without running the model:
`UnrealAi.DumpContextWindow <ThreadGuid> [reason_slug]`

---
## Console commands

From Output Log:

- `UnrealAi.RunCatalogMatrix [filter]` (dispatch only; no LLM)
- `UnrealAi.RunAgentTurn <MessageFilePath> [ThreadGuid] [agent|ask|plan] [OutputDir]`
- `UnrealAi.DumpContextWindow <ThreadGuid> [reason]`

---
## Fresh start (avoid stale harness state)

1. After changing API keys or provider settings, reload LLM configuration from the plugin settings UI (or restart the editor) so the HTTP transport picks up changes.
2. Start a new conversation thread by omitting `ThreadGuid` (or reuse one intentionally to continue persisted context).
3. If you drive via automation/ExecCmds, keep `-ExecCmds="..."` argument splitting compatible with `build-editor.ps1` (quote as a single argv token).

---
## Commands & scripts cheat sheet

- Build BlankEditor + plugin:
  - `.\build-editor.ps1 -Headless`
  - If DLL lock issue: `.\build-editor.ps1 -Restart -Headless`
- Headless CI:
  - `.\build-editor.ps1 -AutomationTests -Headless -SkipBlueprintFormatterSync`
- Headed smoke: matrix + two harness turns + asserts:
  - `.\build-editor.ps1 -HeadedScenarioSmoke -SkipBlueprintFormatterSync`
- Live qualitative:
  - `.\scripts\run-headed-live-scenarios.ps1`
- Context workflows:
  - `.\scripts\run-headed-context-workflows.ps1`
- Full test runner:
  - `.\tests\run-unreal-ai-tests.ps1`
- Assert last/specific JSONL:
  - `python tests\assert_harness_run.py Saved\UnrealAiEditor\HarnessRuns\<ts>\run.jsonl --expect-tool <id> --require-success`

---
## File map (where to edit)

| Area | Path |
|------|------|
| System prompt chunks | `Plugins/UnrealAiEditor/prompts/chunks/` |
| Tool definitions | `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json` |
| Tool implementation | `Plugins/UnrealAiEditor/Source/.../Tools/UnrealAiToolDispatch*.cpp` |
| Matrix fixtures | `tests/fixtures/<tool_id>.json` |
| Headless prompt routing test | `tests/tool-call-prompts.generated.json` (prompt text must contain the tool id substring) |
| Headed scenario prompts | `tests/harness_scenarios/` (`user_scenario_*.txt` message files for `UnrealAi.RunAgentTurn`) |
| Context workflows | `tests/context_workflows/` |
| Domain coverage matrix | `tests/domain_coverage_matrix.md` |
| Qualitative turn template | `tests/qualitative_turn_review_template.md` |

