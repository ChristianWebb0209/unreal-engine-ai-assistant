# Live headed harness (qualitative tier)

This tier runs **headed** Unreal Editor with the **same harness path as Agent Chat** (`UnrealAi.RunAgentTurn`), using **real LLM credentials** from Unreal AI Editor settings. It is **not** a substitute for CI: use it for **human or external LLM** review of behavior, tool routing, and (optionally) **context window dumps**.

**Related:** [AGENT_HARNESS_HANDOFF.md](./AGENT_HARNESS_HANDOFF.md) (tiers), [AGENT_HARNESS_TESTING.md](./AGENT_HARNESS_TESTING.md#deterministic-llm-fixture-transport) (fixture transport), [tool-goals.md](./tool-goals.md) (scenario prompts). Multi-turn **context manager** workflows: [CONTEXT_HARNESS.md](./CONTEXT_HARNESS.md).

---

## Prerequisites

- **Unset** `UNREAL_AI_LLM_FIXTURE` before launching the editor for this tier (the live driver errors if it is set unless you pass `-AllowFixture` intentionally).
- API key / provider configured in **AI Settings** (or your usual env for the editor).
- **Time and cost:** Full `tests/live_scenarios/manifest.json` runs 15 agent tasks; use `-MaxScenarios` or a trimmed manifest for smoke.
- **Baseline project state:** PIE, assets, and level content affect outcomes; document your starting state when comparing runs.

---

## Running the driver

From the repo root (PowerShell):

```powershell
# List scenarios (no editor)
.\scripts\run-headed-live-scenarios.ps1 -DryRun

# First three scenarios only (one editor launch per scenario by default)
.\scripts\run-headed-live-scenarios.ps1 -MaxScenarios 3

# Optional: catalog matrix first, then scenarios (omit matrix with -SkipMatrix)
.\scripts\run-headed-live-scenarios.ps1 -MatrixFilter "blueprint"

# Optional: dump context after each tool (same env the editor reads)
$env:UNREAL_AI_HARNESS_DUMP_CONTEXT = '1'
.\scripts\run-headed-live-scenarios.ps1 -MaxScenarios 1 -DumpContext

# Faster: one launch, chained ExecCmds (one failure may block the rest)
.\scripts\run-headed-live-scenarios.ps1 -SingleSession -MaxScenarios 3
```

Parameters include `-EngineRoot`, `-ProjectRoot`, `-Manifest`, `-SkipMatrix`, `-DryRun`, `-AllowFixture`. See `scripts/run-headed-live-scenarios.ps1` header comments.

---

## Artifacts

- **Harness output (default location):** `Saved/UnrealAiEditor/HarnessRuns/<timestamp>/run.jsonl`
- **Context dumps (when enabled):** `context_window_*.txt` in the same folder as `run.jsonl` (set `UNREAL_AI_HARNESS_DUMP_CONTEXT=1` in the environment, or pass `dumpcontext` as the fifth argument to `UnrealAi.RunAgentTurn`; see console help).
- **Stable copy for review:** `tests/out/live_runs/<suite_id>/<scenario_id>/` (after `--` the driver copies each run).

---

## Qualitative bundle

Aggregate `run.jsonl` and context paths into `review.md` / `review.json`:

```powershell
python tests\bundle_live_harness_review.py tests\out\live_runs\tool_goals
```

Optional structural checks (not automatic pass/fail on “quality”):

```powershell
python tests\assert_harness_run.py tests\out\live_runs\tool_goals\<scenario_id>\run.jsonl
```

---

## Scenario corpus

- **Manifest and prompts:** `tests/live_scenarios/` — see `README.md` there.
- Prompts mirror [tool-goals.md](./tool-goals.md).

---

## Future: context-manager–focused runs

Same infrastructure: enable per-tool context dumps and compare `context_window_*.txt` across steps (manual or scripted diff). No extra console command is required for this phase; a dedicated `UnrealAi.DumpContextWindow` could be added later if snapshots are needed without a full agent turn.
