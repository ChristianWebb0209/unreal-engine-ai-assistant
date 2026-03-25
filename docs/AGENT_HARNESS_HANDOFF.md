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
4. Matrix fixtures (when tools need non-empty args to be meaningful)
5. Harness fixtures (multi-round `responses[]` stream correctness)

---
## Three harness tiers

### 1a — CI / contract (headless)

`.\build-editor.ps1 -AutomationTests -Headless -SkipBlueprintFormatterSync`

### 1b — Fixture harness (deterministic LLM)

Set `UNREAL_AI_LLM_FIXTURE` **before** launching the editor so the plugin uses the deterministic fixture transport.

- Schema: `tests/harness_llm_fixture.schema.json`
- Example: `tests/harness_llm_fixture.example.json`
- Each `StreamChatCompletion` call consumes the **next** object in `responses[]` (the number of `responses[]` entries should match the number of LLM rounds your scenario needs).

### 2 — Live headed qualitative (real API)

Headed, qualitative runs using real API credentials from Unreal AI Editor settings.

- Ensure `UNREAL_AI_LLM_FIXTURE` is **unset** (or pass `-AllowFixture` intentionally).
- Run: `.\scripts\run-headed-live-scenarios.ps1 -MaxScenarios <N>`
- Optional context dumps: `UNREAL_AI_HARNESS_DUMP_CONTEXT=1`

Artifacts:
- Harness output: `Saved/UnrealAiEditor/HarnessRuns/<timestamp>/run.jsonl`
- Stable review copies: `tests/out/live_runs/<suite_id>/<scenario_id>/`

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
- `UnrealAi.RunAgentTurn <MessageFilePath> [ThreadGuid] [agent|ask|orchestrate] [OutputDir]`
- `UnrealAi.DumpContextWindow <ThreadGuid> [reason]`

---
## Fresh start (avoid stale harness state)

1. Restart Unreal after changing `UNREAL_AI_LLM_FIXTURE` (the backend registry must rebuild transport).
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
| Headed scenarios | `tests/harness_scenarios/` (`user_scenario_*.txt`, `responses[]` consumed per LLM round) |
| Context workflows | `tests/context_workflows/` |

