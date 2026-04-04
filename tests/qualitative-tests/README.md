# Qualitative headed harness (suites)

Runs multi-turn LLM conversations inside **headed** Unreal Editor with full `run.jsonl` artifacts.

## Quick start

From repo root (requires API keys in Unreal AI Editor settings):

```powershell
.\tests\qualitative-tests\run-qualitative-headed.ps1 -Suite blueprint-creation-curriculum-v1
```

Suite definitions live under [`suites/`](suites/) and are tracked in git (see repo `.gitignore`).

## Suite index

| File | Kind | Turns (approx.) | Notes |
|------|------|-----------------|--------|
| [`blueprint-creation-curriculum-v1.json`](suites/blueprint-creation-curriculum-v1.json) | Curriculum / stress | 10 | Long vague blueprint authoring thread; see [`docs/tooling/BLUEPRINT_CURRICULUM_AGENT_LOOP.md`](../../docs/tooling/BLUEPRINT_CURRICULUM_AGENT_LOOP.md) |
| [`blueprint-builder-edge-curriculum-v1.json`](suites/blueprint-builder-edge-curriculum-v1.json) | Curriculum / manual triage | 5 | Blueprint Builder policy edges: delegation language, compile/gate messaging, recap (qualitative only) |
| [`path-focus-mini.json`](suites/path-focus-mini.json) | Smoke | 2 | Underspecified path + follow-up |
| [`passed-tests.json`](suites/passed-tests.json) | Regression | many | High-confidence passes; low flake |
| [`regression-watchlist.json`](suites/regression-watchlist.json) | Regression / watchlist | many | Project-sensitive or chain-heavy turns |
| [`pre-release-natural-gaps.json`](suites/pre-release-natural-gaps.json) | Stress | varies | Natural-language gaps |
| [`context-continuity-corridor-build.json`](suites/context-continuity-corridor-build.json) | Stress | varies | Continuity / corridor |
| [`new-test-basket-02.json`](suites/new-test-basket-02.json) | Fragment | 5 | Part of a multi-part basket series (historical baselines) |

## Blueprint creation curriculum

- **Suite:** [`suites/blueprint-creation-curriculum-v1.json`](suites/blueprint-creation-curriculum-v1.json) — 10 `agent` turns (discovery, create BP, variables, graph patch/IR, custom event, reroute, branch, format, summary).
- **Iteration guide** (run → analyze → fix prompts/tooling → graduate easy turns → refill to 10): [`../../docs/tooling/BLUEPRINT_CURRICULUM_AGENT_LOOP.md`](../../docs/tooling/BLUEPRINT_CURRICULUM_AGENT_LOOP.md)

## Parent doc

[`../../docs/tooling/AGENT_HARNESS_HANDOFF.md`](../../docs/tooling/AGENT_HARNESS_HANDOFF.md)

## Post-run tools

```bash
python tests/classify_harness_run_jsonl.py --batch-root tests/qualitative-tests/runs/<run-folder>
python tests/assert_harness_run.py <path-to-run.jsonl> --expect-tool blueprint_compile --require-success
```

Strict (deterministic) suites live under [`../strict-tests/`](../strict-tests/).
