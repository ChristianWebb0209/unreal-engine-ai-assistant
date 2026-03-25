# Live headed scenarios (`tests/live_scenarios`)

**Maintainer-only:** prompts and manifests here support **internal** qualitative runs (real API, headed editor)—not an end-user feature.

This folder holds **manifest-driven prompts** for the **live headed qualitative** harness tier (see [docs/AGENT_HARNESS_HANDOFF.md](../../docs/AGENT_HARNESS_HANDOFF.md)).

## What this is not

- **Not** CI: runs use **real API** credentials from Unreal AI Editor settings (or env, per your setup). Do **not** set `UNREAL_AI_LLM_FIXTURE` for these runs.
- **Not** a pass/fail gate: outcomes are reviewed **qualitatively** (human or external LLM). Use `tests/assert_harness_run.py` only for optional structural checks.

## Cost and time

Full suite = many agent turns × API calls. Use `-MaxScenarios` or a subset manifest before burning tokens.

## Manifest

- `manifest.json` — `suite_id`, `scenarios[]` with `id`, `source_task`, `message_file` (UTF-8 `.txt` under this folder), optional `mode` (`agent` default).
- Prompts mirror [docs/tool-goals.md](../../docs/tool-goals.md).

## Known gaps

See [docs/tool-goals.md](../../docs/tool-goals.md) for known tooling gaps.

## Running

From repo root:

```powershell
.\scripts\run-headed-live-scenarios.ps1
.\scripts\run-headed-live-scenarios.ps1 -MaxScenarios 3 -DryRun
```

Set `UNREAL_AI_HARNESS_DUMP_CONTEXT=1` in the environment (or pass `dumpcontext` as the 5th arg to `UnrealAi.RunAgentTurn`) to emit `context_window_*.txt` next to `run.jsonl` for review.
