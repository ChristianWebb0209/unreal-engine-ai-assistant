# Qualitative headed harness (suites)

Runs multi-turn LLM conversations inside **headed** Unreal Editor with full `run.jsonl` artifacts.

## Quick start

From repo root (requires API keys in Unreal AI Editor settings):

```powershell
.\tests\qualitative-tests\run-qualitative-headed.ps1 -Suite blueprint-creation-curriculum-v1
```

Other suites live under [`suites/`](suites/).

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
