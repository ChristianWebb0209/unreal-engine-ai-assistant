# Context workflows (qualitative context manager tier)

**Maintainer-only:** multi-turn harness material for **internal** context-manager review—not a supported user workflow.

Multi-turn **workflows** for headed Unreal Editor runs that reuse a **single thread id** across steps so `FUnrealAiContextService` state matches long chat sessions. Use with **`UNREAL_AI_HARNESS_DUMP_CONTEXT=1`** (or `-DumpContext` on the driver) to emit `context_window_*.txt` for qualitative review.

**Not CI.** Optional structural checks only. Prefer **`UNREAL_AI_LLM_FIXTURE` unset** for realistic behavior; use a fixture only for cheap plumbing checks (`-AllowFixture` on the script).

## Layout

| Path | Purpose |
|------|---------|
| [`suite.json`](suite.json) | Lists workflow manifests under `workflows/`. |
| [`workflow.schema.json`](workflow.schema.json) | JSON Schema for a single `workflow.json`. |
| `workflows/<id>/workflow.json` | One workflow: `steps[]` with `message_file` relative to that folder. |
| `workflows/<id>/prompts/*.txt` | UTF-8 user messages per step. |

## Cost and time

Each step is a full **`UnrealAi.RunAgentTurn`** (real API when fixture is off). Multi-step workflows multiply cost. Use `-MaxWorkflows`, `-MaxSteps`, or run a single workflow manifest.

## Human-assisted scenarios

Some workflows (e.g. editor selection changes) may require the reviewer to act in the editor between steps. Those are documented in per-workflow `notes` when we add them.

## Running

From repo root:

```powershell
.\scripts\run-headed-context-workflows.ps1 -DryRun
.\scripts\run-headed-context-workflows.ps1 -SuiteManifest tests\context_workflows\suite.json -DumpContext -MaxSteps 1
.\scripts\run-headed-context-workflows.ps1 -WorkflowManifest tests\context_workflows\workflows\conv_memory_smoke\workflow.json
```

Artifacts: `tests/out/context_runs/<suite_id>/<workflow_id>/step_*`.

Bundle for review:

```powershell
python tests\bundle_context_workflow_review.py tests\out\context_runs\context_pilots\conv_memory_smoke
```

Optional **snapshot without a full LLM turn** (after at least one turn has loaded the thread): editor console `UnrealAi.DumpContextWindow <ThreadGuid> [reason]` — see [docs/CONTEXT_HARNESS.md](../../docs/CONTEXT_HARNESS.md).

## Known gaps

[docs/TOOLING_FOLLOWUPS.md](../../docs/TOOLING_FOLLOWUPS.md)
