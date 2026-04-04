# Strict Test Harness

The problem: "long-running-tests" was an inaccurate name since those suites are qualitative evaluations.

Solution: This test harness will have a different suite.json schema, so each query will have a separate object of assertions to be made afterwards. These assertions are deterministic and run against editor-side state (no qualitative/LLM-freeform checks).

Examples:

- Assert that a file was created
- Assert that a given node was made in a blueprint
- Assert that lines of code exist in a created cpp script
- Assert that a certain attribute was modified on a component
- Assert that a certain asset was renamed

## Strict vs agent tool path

- **Agent turns** (`UnrealAi.RunAgentTurn`) execute tools through the harness. For main-agent Blueprint work, some tools (for example `blueprint_compile`) are **withheld** and recorded in `run.jsonl` as `tool_finish` with `success: false` and a `blueprint_tool_withheld` message.
- **`tool_invoke_ok`** in `UnrealAi.RunStrictAssertions` calls `IToolExecutionHost::InvokeTool` **directly**. It does **not** replay the agent stream and does **not** apply the same withhold rules. It answers: “does this tool succeed against current editor state?”
- To assert **agent** behavior, use **`run_jsonl_last_tool_finish`**, **`run_jsonl_enforcement_event`**, or **`run_jsonl_substring`** against the turn’s `run.jsonl` (same folder as `strict_assertions_result.json`).
- Typical pattern for gated tools: assert the **last** matching `tool_finish` in `run.jsonl` shows the expected failure, then assert **`tool_invoke_ok`** for the same tool to prove the underlying asset/editor state is still valid.

Implementation: [`Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/UnrealAiEditorModule.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/UnrealAiEditorModule.cpp) (`UnrealAi.RunStrictAssertions`).

## How this harness works

- `run-strict-headed.ps1` wraps `tests/qualitative-tests/run-qualitative-headed.ps1`.
- The qualitative headed runner supports a per-turn `assertions[]` array in strict suite files.
- For each turn:
  - The runner writes `assertions.json` into the turn `step_XX` directory.
  - It then invokes an editor-side console command `UnrealAi.RunStrictAssertions` immediately after the headed turn.
  - The console command executes deterministic checks by invoking editor-side tools/state and writes `strict_assertions_result.json` into the same `step_XX` directory.
- Batch pass/fail is driven by strict assertion results:
  - If any turn with `assertions[]` fails strict assertions, the suite is marked failed (and the overall batch exits non-zero if any suite fails).

## Strict suite schema

```json
{
  "suite_id": "strict_basic_smoke_v1",
  "default_type": "agent",
  "turns": [
    {
      "id": "turn_01",
      "type": "agent",
      "request": "Read Config/DefaultEngine.ini and summarize map settings.",
      "assertions": [
        { "type": "tool_invoke_ok",
          "tool": "project_file_read_text",
          "arguments": { "relative_path": "Config/DefaultEngine.ini" }
        }
      ]
    }
  ]
}
```

## Supported assertion types

All handlers live in `UnrealAi.RunStrictAssertions` unless noted.

| Type | Purpose |
|------|---------|
| `asset_exists` | `object_path` — asset loads in editor |
| `asset_not_exists` | `object_path` — asset does not load |
| `project_file_exists` | `relative_path` — file under project dir |
| `project_dir_exists` | `relative_path` — directory under project dir |
| `tool_invoke_ok` | `tool`, `arguments` — direct tool invocation succeeds (`Inv.bOk`) |
| `tool_invoke_fail` | `tool`, `arguments` — direct invocation fails as expected |
| `tool_result_path_exists` | `tool`, `arguments`, `path` — JSON path exists in tool result |
| `tool_result_path_equals_string` | `tool`, `arguments`, `path`, `expected` |
| `tool_result_path_contains` | `tool`, `arguments`, `path`, `substring` |
| `tool_result_array_min_length` | `tool`, `arguments`, `path`, `min_length` |
| `tool_result_number_gte` / `tool_result_number_lte` | numeric comparisons at `path` |
| `tool_result_bool_equals` | bool at `path` |
| `tool_result_string_nonempty` | string at `path` non-empty |
| `blueprint_export_ir_node_count_min` | `blueprint_path`, `min_nodes`, optional `graph_name` |
| `run_jsonl_last_tool_finish` | `tool`, `success` (bool), optional `result_preview_contains`, optional `run_jsonl_relative_path` (default `run.jsonl`) — last matching `tool_finish` line in the turn’s JSONL |
| `run_jsonl_enforcement_event` | `event_type`, optional `detail_contains`, optional `run_jsonl_relative_path` — first matching `enforcement_event` |
| `run_jsonl_substring` | `substring`, optional `run_jsonl_relative_path` — file body contains substring |

## Usage

```powershell
.\tests\strict-tests\run-strict-headed.ps1 -Suite strict_blueprint_creation_v2
.\tests\strict-tests\run-strict-headed.ps1 -Suite strict_blueprint_builder_edges_v1
```

Options:

- `-Suite` (required, with or without `.json`)
- `-MaxSuites` (default `0` = all)
- `-DryRun`

## Notes

- This harness is designed to expose regressions quickly even if long-running qualitative runs appear green, because strict assertions check editor-side outcomes directly.
- Agent turns that **never** call a tool referenced by `run_jsonl_last_tool_finish` will fail that assertion (no matching `tool_finish` line).
