# Context Manager Shortcomings (Workflow Audit)

Date: 2026-03-26  
Workflow audited: `tests/context_workflows/workflows/conv_memory_smoke/workflow.json`  
Artifacts:
- `tests/out/context_runs/context_pilots/conv_memory_smoke/context_review.json`
- `tests/out/context_runs/context_pilots/conv_memory_smoke/step_01_remember_codeword/run.jsonl`
- `tests/out/context_runs/context_pilots/conv_memory_smoke/step_01_remember_codeword/context_build_trace_run_started.json`
- `tests/out/context_runs/context_pilots/conv_memory_smoke/step_01_remember_codeword/context_decision_logs/*.jsonl`

## Scope and run notes

- The workflow runner repeatedly hung during editor turn execution (step 2 in a two-step run, and a separate one-step rerun), requiring manual process termination.
- At least one full step completed with usable `run.jsonl`, context traces, and decision logs, so context-manager behavior could still be evaluated.

## Findings

### 1) Observability mismatch: dumped context window does not match request-time ranked context
Status: **partially resolved**

Evidence:
- `context_window_run_started.txt` is short and contains only recent UI + engine + basic snapshot lines.
- Decision logs for the same step include `memory_snippet` candidates and higher kept counts than the dump text suggests.

Likely cause:
- Harness context dumps call `BuildContextWindow` without the same request-time options (notably `UserMessageForComplexity`), while request assembly calls it with richer options.

Impact:
- Tuning via `context_window_*.txt` alone can be misleading; a reviewer may optimize the wrong signals because request-time candidate behavior is not directly visible in that artifact.

Update:
- Decision logs are now stamped with `invocationReason` and request builds emit `request_build`, so bundle metrics can separate request-time ranking from harness dumps.
- We still do not emit a dedicated request-time context text dump artifact; that remains optional future work.

### 2) Memory candidates can dominate even for simple turns
Status: **open**

Evidence:
- Decision logs show `memory_snippet` totals around the same band as top live signals (`recent_tab`) in at least one capture.

Likely cause:
- Memory base importance + semantic score flooring can make durable memories very competitive even when the current turn is short and local.

Impact:
- Risk of over-injecting historical memory on simple tasks, increasing token use and potentially distracting the model from immediate editor context.

Recommendation:
- Add a contextual dampener for memory scoring on low-complexity turns (or require stronger lexical overlap / explicit recency interaction before packing memories).

### 3) No dropped-candidate pressure in the audited run
Status: **resolved**

Evidence:
- Ranking metrics report `dropped_total: 0` for this run.

Likely cause:
- Budget is large relative to candidate volume for this workflow.

Impact:
- This workflow currently cannot validate cap behavior (`pack:per_type_cap`) or budget trade-offs (`pack:budget`), so it is weak for tuning hardcoded weights/caps.

Update:
- Added `tests/context_workflows/workflows/ranker_pressure_competition/` and included it in `tests/context_workflows/suite.json`.
- Workflow includes expected drop-reason checks for pressure validation.

### 4) Decision-log aggregation can overcount across multiple context builds in one step
Status: **resolved**

Evidence:
- `kept_total` in aggregated ranking metrics exceeds what a single context window dump implies.

Likely cause:
- Multiple `BuildContextWindow` invocations per step (run start dump, request build, run finish dump) are all collected into the same metric bucket.

Impact:
- Per-step metrics are noisy for tuning unless grouped by invocation reason.

Update:
- Decision logs now include `invocationReason`.
- Bundlers now headline `request_build` metrics and provide all-invocations diagnostics separately.

### 5) Workflow execution reliability issue blocks repeatable tuning loops
Status: **resolved**

Evidence:
- `run-headed-context-workflows.ps1` hangs in this environment during step execution; manual process kill required.

Impact:
- Hard to run repeated A/B sweeps; expensive human intervention reduces throughput and confidence in comparisons.

Update:
- `run-headed-context-workflows.ps1` now includes per-step retry handling and writes `step_status.json` and `workflow_status.json` artifacts.

### 6) Thread id artifact includes UTF-8 BOM
Status: **resolved**

Evidence:
- `context_review.json` thread id starts with a BOM marker.

Likely cause:
- `thread_id.txt` written with BOM-bearing UTF-8 and consumed without trimming BOM.

Impact:
- Can break exact string matching for thread-scoped joins (including decision-log correlation and tooling scripts).

Update:
- `thread_id.txt` is now written without BOM.
- Bundle reader normalizes leading BOM when parsing thread id text.

## Does current context manager support fine tuning?

Partially.

- Strengths: centralized ranking constants, structured decision logs, and workflow harness artifacts are all present.
- Current blockers: memory-vs-live-context weighting calibration (and optional request-time context text snapshots for convenience).

Net: the foundation is strong, but trustworthy high-velocity tuning requires the instrumentation and workflow reliability fixes above.
