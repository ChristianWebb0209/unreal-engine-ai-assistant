# Context manager qualitative harness

This tier exercises **multi-turn** behavior on a **single thread id** so `FUnrealAiContextService` state evolves like a long Agent Chat session. It complements:

- **Fixture / CI tiers** — matrix, headless automation, headed smoke with `UNREAL_AI_LLM_FIXTURE` ([AGENT_HARNESS_HANDOFF.md](./AGENT_HARNESS_HANDOFF.md)).
- **Live tool-goals tier** — single-shot scenarios from `tests/live_scenarios/` ([LIVE_HARNESS.md](./LIVE_HARNESS.md)).

**This tier** — `tests/context_workflows/` + `scripts/run-headed-context-workflows.ps1` — focuses on **what enters and leaves** the built context window over successive `UnrealAi.RunAgentTurn` calls.

---

## Prerequisites

- **Unset** `UNREAL_AI_LLM_FIXTURE` for realistic LLM + context behavior (or pass `-AllowFixture` only for plumbing checks).
- API credentials in **AI Settings** (or your usual setup).
- **Cost:** Each **step** is a full harness turn; multi-step workflows multiply API usage.

---

## Artifacts

| Location | Contents |
|----------|----------|
| `Saved/UnrealAiEditor/HarnessRuns/<ts>/` | Raw per-step harness output (`run.jsonl`, optional `context_window_*.txt`). |
| `tests/out/context_runs/<suite_id>/<workflow_id>/` | Stable copy: `workflow.json`, `thread_id.txt`, `step_NN_<id>/` per step. |
| `context_review.md` / `context_review.json` | Produced by `tests/bundle_context_workflow_review.py`. |

Enable context dumps: **`UNREAL_AI_HARNESS_DUMP_CONTEXT=1`** or **`-DumpContext`** on the script (same as [LIVE_HARNESS.md](./LIVE_HARNESS.md)).

---

## Running

```powershell
# List workflows and steps (no editor)
.\scripts\run-headed-context-workflows.ps1 -DryRun

# Full pilot suite (headed, real API)
.\scripts\run-headed-context-workflows.ps1 -SuiteManifest tests\context_workflows\suite.json -DumpContext

# One workflow, first step only
.\scripts\run-headed-context-workflows.ps1 -WorkflowManifest tests\context_workflows\workflows\conv_memory_smoke\workflow.json -MaxSteps 1 -DumpContext

# Faster: one editor launch per workflow (chained ExecCmds)
.\scripts\run-headed-context-workflows.ps1 -WorkflowManifest tests\context_workflows\workflows\conv_memory_smoke\workflow.json -SingleSession -DumpContext
```

Optional catalog matrix first: omit **`-SkipMatrix`** (default runs `UnrealAi.RunCatalogMatrix` once before workflows).

---

## Qualitative bundle (diff between steps)

```powershell
python tests\bundle_context_workflow_review.py tests\out\context_runs\context_pilots\conv_memory_smoke
```

This lists tools per step and a **unified diff** of `context_window_run_finished.txt` between consecutive steps (truncate very large diffs in the viewer as needed). It does **not** auto-judge quality.

---

## Snapshot without a full LLM turn

After a thread has been loaded (e.g. after at least one `UnrealAi.RunAgentTurn` in the same editor session), you can write the current built context to disk without running the model:

```
UnrealAi.DumpContextWindow <ThreadGuid> [reason_slug]
```

Output: `Saved/UnrealAiEditor/ContextSnapshots/<reason>_<timestamp>.txt`

Use the **thread id** from `tests/out/context_runs/.../thread_id.txt` for that workflow.

---

## Limitations

- **Flakiness:** Editor selection, PIE, and assets affect snapshots; some workflows may need **human** steps (documented in workflow `notes` when applicable).
- **Cross-process persistence:** Prefer **`-SingleSession`** or a **single** editor session per workflow if you rely on in-memory-only behavior; otherwise persistence should match normal chat.
- **Orchestrate / todo stress:** Add more workflows under `tests/context_workflows/workflows/` as needed; keep manifests expandable.

---

## Corpus layout

See [tests/context_workflows/README.md](../tests/context_workflows/README.md).
