# Strict Test Harness

The problem: "long-running-tests" was an inaccurate name since those suites are qualitative evaluations.

Solution: This test harness will have a different suite.json schema, so each query will have a separate object of assertions to be made afterwards. These assertions are deterministic and run against editor-side state (no qualitative/LLM-freeform checks).

Examples:

- Assert that a file was created
- Assert that a given node was made in a blueprint
- Assert that lines of code exist in a created cpp script
- Assert that a certain attribute was modified on a component
- Assert that a certain asset was renamed

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

- `asset_exists`
  - Fields: `object_path` (string) (for example `/Game/Blueprints/MyBP.MyBP`)
- `tool_invoke_ok`
  - Fields: `tool` (string, tool id), `arguments` (JSON object passed to the tool)
- `blueprint_export_ir_node_count_min`
  - Fields: `blueprint_path` (string), `min_nodes` (int), optional `graph_name` (string)
  - Uses `blueprint_export_ir` editor tool and verifies the exported IR contains at least `min_nodes` nodes.

## Usage

```powershell
.\tests\strict-tests\run-strict-headed.ps1 -Suite strict_blueprint_creation_v2
```

Options:

- `-Suite` (required, with or without `.json`)
- `-MaxSuites` (default `0` = all)
- `-DryRun`
- `-KeepTempSuites`

## Notes

- This harness is designed to expose regressions quickly even if long-running qualitative runs appear green, because strict assertions check editor-side outcomes directly.

