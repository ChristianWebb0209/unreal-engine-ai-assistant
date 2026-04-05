# Headed long-running harness — analysis guide

This document is for **future agents and humans** who need to **find the latest headed batch**, **interpret artifacts**, and **turn failures into concrete improvements**. It aligns with how this repo records quality work in [`tool-iteration-log.md`](tool-iteration-log.md) (freeform dated entries).

---

## What “testing” means here

Unless someone names something else, **“testing”** in this repo often means:

- **Primary:** [`tests/qualitative-tests/`](.) — headed Unreal batches driven by [`run-qualitative-headed.ps1`](run-qualitative-headed.ps1), suite JSON under subfolders (e.g. `realistic-user-agent-basket`, `plan-mode-smoke`, `mixed-salad-complex`), outputs under [`runs/`](runs/).

**Not** the default meaning: plugin `Automation` tests only, or ad-hoc Python scripts, unless explicitly requested.

---

## Where runs live

- **Implementation note:** Plan-mode DAG parsing, execution, and shared plan/todo summaries live under `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Planning/` (not under `Harness/`).
- **Root for all batches:** [`tests/qualitative-tests/runs/`](runs/)
- Each invocation creates a folder:

  `runs/run-<monotonic-index>-<local-date-time>/`

  Example: `runs/run-13-20260328-191756_017/`

- **Inside a batch folder:**
  - `last-suite-summary.json` — batch metadata, suite list, paths to each suite’s `run_root`, `thread_id`, `exec_parts`, `expected_run_jsonls`, per-turn `step_dir`.
  - `harness-classification.json` — optional; produced when the classifier runs (see below).
  - Per-suite slug folder (often `suite/` if one suite) with:
    - `summary.json`, `workflow-input.json` (copy of suite source)
    - `turn_messages/turn_XX.txt`
    - `turns/step_XX/run.jsonl` — primary forensic record for turn `XX`
    - `turns/step_XX/context_*.txt`, `context_decision_logs/` as configured
    - `editor_console_saved.log` / `editor_console_full_batch.log` (session logging)

---

## How to get the **latest** run (do not guess)

The **authoritative** “newest batch” is the directory under `runs/` with the **highest `run-<N>-…` index** `N`, not “the file Cursor opened last week.”

**Practical steps (Windows / PowerShell from repo root):**

```powershell
Get-ChildItem -LiteralPath "tests\qualitative-tests\runs" -Directory |
  Where-Object { $_.Name -match '^run-(\d+)-' } |
  Sort-Object { [int]$Matches[1] } -Descending |
  Select-Object -First 5 Name, LastWriteTime
```

Pick the **top** `Name` — that is the latest batch unless the user renamed folders.

**Caveats:**

- A **`DryRun`** also creates a batch folder (smaller, no editor); check `last-suite-summary.json` for `"dry_run": true` if you need a real headed run only.
- [`tests/qualitative-tests/last-suite-summary.json`](../last-suite-summary.json) (if present at repo root) may **lag** or point at an older batch; **always confirm** against the `runs/` folder with the highest index.
- Multiple suites in one batch share one `run-<N>-…` folder; each suite has its own subfolder under that batch.

---

## Quick analysis workflow

1. **Open** `runs/run-<N>-…/last-suite-summary.json`  
   - Note `scenario_folder`, `batch_stamp`, `suite_file_count`, `editor_exit_code`, `batch_all_expected_run_jsonls_finished`, `all_turns_reached_terminal`, `failed_suite_count`.

2. **For each suite** in `runs`, follow `run_root` → open that folder’s `summary.json` for turn list and flags.

3. **Scan each** `turns/step_XX/run.jsonl` for:
   - Last line: `"type":"run_finished"` — `success` true/false and `error_message`.
   - `"type":"harness_sync_timeout_diagnostic"` — sync wait hit (often 300s per segment in code; see `UnrealAiWaitTimePolicy.h`).
   - `"type":"harness_sync_idle_abort_diagnostic"` — idle abort path.
   - `"type":"continuation"` — plan phases / multi-segment progress.
   - `"type":"tool_finish"` with `"success":false` — resolver or model args issue.
   - `"type":"enforcement_event"` — harness policy, action-intent notes, etc.

4. **Optional aggregate classification** (requires Python 3):

   ```text
   python tests/classify_harness_run_jsonl.py --batch-root tests/qualitative-tests/runs/run_<N>-<timestamp>
   ```

   Or from a batch’s `last-suite-summary.json`:

   ```text
   python tests/classify_harness_run_jsonl.py --from-summary tests/qualitative-tests/runs/run_<N>-<timestamp>/last-suite-summary.json
   ```

   Buckets include `run_finished_ok`, `tool_finish_false`, `http_timeout`, `harness_policy`, `rate_limit`, `invalid_request`, etc. (see [`tests/classify_harness_run_jsonl.py`](../../tests/classify_harness_run_jsonl.py)).

5. **Correlate with editor logs** under the suite folder or `Saved/Logs` on the machine that ran the editor — for **crashes**, **modal dialogs**, or **UE_LOG** not mirrored to `run.jsonl`.

6. **Record conclusions** in [`tool-iteration-log.md`](tool-iteration-log.md) under the four areas below (short bullets, what changed / what you observed).

---

## The four main areas of concern (improvement taxonomy)

Improvements almost always fall into **one or more** of these. Use them to **classify** failures and to **structure** recommendations and doc entries.

### 1. Context

**What it covers:** Thread and project context assembly — retrieval, compaction, mentions, snapshots, **active plan DAG** state, **trimming** API messages (context budget), **orphan tool rows** after trim (HTTP 400s), anything in [`Context/`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/) and related prompt assembly.

**Symptoms in artifacts:** Wrong or missing editor state in the model’s view; 400 errors mentioning `tool` / `tool_calls` ordering; huge or truncated histories; retrieval prefetch errors.

**Typical fixes:** Adjust context builders, trim rules, mention parsing; **not** random tool dispatch changes unless the error is clearly tool-side.

---

### 2. Tools

**What it covers:** **Tool catalog** surface ([`tools.main.json`](../../Plugins/UnrealAiEditor/Resources/tools.main.json)), **tool schemas**, **modes** (ask / agent / plan), **core pack** and routing, **tiered / dispatch** surfaces ([`UnrealAiToolSurfacePipeline`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolSurfacePipeline.cpp)), **tool matrices** and smoke runners.

**Symptoms:** Wrong tool in roster; model never sees a tool; duplicate `fast` mode confusion; `tool_start` for wrong `tool_id`.

**Typical fixes:** Catalog copy, `modes`, summaries, **per-tool** nudges (prefer localized catalog lines over bloating global prompt chunks when possible).

---

### 3. Resolvers

**What it covers:** **Dispatch implementations** — validation, **empty `{}`** rejection, **`ErrorWithSuggestedCall`**, **`suggested_correct_call`** toward discovery tools, **performance** of registry/search (e.g. bounded `EnumerateAssets` vs full `GetAssets`), **viewport** / **asset** path validation.

**Symptoms:** `tool_finish` with `success:false`; repeated identical failures; harness stops after **N** identical failures; minute-long **game-thread stalls** from unbounded queries.

**Typical fixes:** Tighten validation, add **actionable** error text, cap expensive operations, align **catalog promise** with **handler behavior**.

---

### 4. Prompts

**What it covers:** **System/developer** chunks ([`Plugins/UnrealAiEditor/prompts/chunks/`](../../Plugins/UnrealAiEditor/prompts/chunks/)), [`UnrealAiPromptBuilder`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Prompt/UnrealAiPromptBuilder.cpp), **plan DAG** chunk (`09-plan-dag.md`), **tool-calling contract** (`04-tool-calling-contract.md`), **plan-mode planner JSON-only** rules (no `tool_calls` in planner pass when product is DAG-only).

**Symptoms:** Model emits **`{}`** for required fields; emits **tools** when planner should output **JSON only**; ignores schema; verbose or contradictory instructions.

**Typical fixes:** Contract edits, examples, plan-mode wording; balance **global token cost** vs **per-tool catalog** nudges.

---

### Cross-cutting (tie these to the four above)

These show up in `run.jsonl` often but are usually **root-caused** under one of the four:

- **Harness / automation / plan executor** — `run_finished` false with sync timeout, `idle_abort_skip_reason`, plan sub-turn counters, `IsTurnInProgress` vs plan pipeline (see `UnrealAiHarnessScenarioRunner`, `FUnrealAiPlanExecutor`, `UnrealAiWaitTimePolicy.h`). File issues under **area 1 or 4** when it’s wait/contract; under **resolver/tool** when it’s stuck in tool execution.
- **HTTP / transport** — long stalls with active request, 429, `http_timeout` in classification. Often **area 1** (request body) or **environment** (API keys, provider); **not** “fix the catalog” unless the request is malformed by design.

---

## Suggesting future improvements (checklist)

- **State the batch:** `run-<N>-…`, suite id, and **which `step_XX`** failed.
- **Quote evidence:** one `run.jsonl` line or diagnostic type, not only a hunch.
- **Map to the four areas** (and cross-cutting if needed).
- **Prefer smallest fix:** catalog line vs global prompt; resolver validation vs new tool; harness timeout vs LLM policy.
- **Avoid:** assuming an old batch is “latest”; conflating **idle abort** with **in-flight HTTP** (idle abort does not cancel active sockets — see `HasActiveLlmTransportRequest` in harness docs and code).
- **Plan-mode:** distinguish **planner** `RunTurn` (Plan mode, tools `[]`) from **node** `RunTurn` (Agent mode); check `continuation` and per-segment sync in `run.jsonl`.

---

## Related files

| Purpose | Location |
|--------|----------|
| Batch runner | [`run-qualitative-headed.ps1`](run-qualitative-headed.ps1) |
| JSONL classifier | [`tests/classify_harness_run_jsonl.py`](../../tests/classify_harness_run_jsonl.py) |
| Wait / sync / HTTP policy | [`UnrealAiWaitTimePolicy.h`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Misc/UnrealAiWaitTimePolicy.h) |
| Ship log | [`tool-iteration-log.md`](tool-iteration-log.md) |
| Agent handoff | [`docs/tooling/AGENT_HARNESS_HANDOFF.md`](../../docs/tooling/AGENT_HARNESS_HANDOFF.md) |

---

*Last updated to reflect headed harness layout, four-area taxonomy, and latest-run discovery by `run-<N>` index.*
