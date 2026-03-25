# Unreal AI Headed Live Harness Learnings

Date: 2026-03-25  
Repo: `C:\Github\ue-plugin`  
Engine: UE 5.7  
Primary goal: run headed harness scenarios against a real API key, iterate quickly, and avoid long "stuck" loops.

## Why this file exists

Future sessions may start with zero history. This document captures what was tried, what failed, what was fixed, what still fails, and exactly how to resume with fast iteration.

---

## Executive Summary

- Real API calls are confirmed to happen (`https://api.openai.com/v1/chat/completions` appears in logs).
- Biggest historical issue was not "no work happening", but poor visibility and very slow failure signals.
- Multiple script and harness changes were made to:
  - avoid brittle `-ExecCmds` quoting for file paths,
  - force fast-fail behavior,
  - cap LLM rounds for quick iterations,
  - make logs explain idle-looking editor behavior.
- Current short-loop behavior is much better (roughly ~1-2 minute feedback), but there is still a timing/completion inconsistency to fix (details below).

---

## What Was Broken (Root Causes)

## 1) `RunAgentTurn` argument parsing broke in `-ExecCmds`

Symptoms:
- UE log showed `Cmd: UnrealAi.RunAgentTurn` without a message file argument.
- Error: "pass a UTF-8 message file as first arg, or set UNREAL_AI_HARNESS_MESSAGE_FILE".

Root cause:
- Quoted path argument inside `-ExecCmds="..."` was brittle in Windows process argument parsing.

Fix:
- Stop passing message file in `ExecCmds`.
- Set `UNREAL_AI_HARNESS_MESSAGE_FILE` env var per launch, then call plain `UnrealAi.RunAgentTurn`.

---

## 2) Command chaining separator confusion in `-ExecCmds`

Symptoms:
- `Cmd: UnrealAi.RunAgentTurn;Quit` or `Cmd: UnrealAi.RunAgentTurn|Quit` appeared as one command string.
- Harness did not behave as expected.

What was verified:
- Comma works in this environment: `-ExecCmds="UnrealAi.RunCatalogMatrix blueprint,Quit"` produced separate:
  - `Cmd: UnrealAi.RunCatalogMatrix blueprint`
  - `Cmd: Quit`

Fix:
- Standardized script chains to use `,Quit`.

---

## 3) Extremely slow "stuck" loops

Symptoms:
- Editor appears idle for long periods.
- User perception: nothing happening for 20-60 minutes.

Root causes:
- Harness sync wait was too long (initially 30 minutes).
- HTTP request timeout was long/default and not optimized for iterative debugging.
- Transport start-failure path could be unclear.
- Round budget could allow many LLM-tool cycles.

Fixes:
- Added explicit logging before LLM submit and at run start.
- Reduced HTTP request timeout (now 15s for fast iteration).
- Reduced sync wait in scenario runner (now 60s).
- Added env override for max rounds and used low defaults in scripts (2).
- Added `ProcessRequest()` start check and immediate error emit if start fails.

---

## 4) Script syntax instability from Unicode punctuation

Symptoms:
- PowerShell parse errors like missing terminator.

Root cause:
- Non-ASCII punctuation in script strings (`—`, `▸`) under some shells/encodings.

Fix:
- Use ASCII-safe text in script messages.

---

## Changes Applied

## C++ / Plugin behavior

### `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Transport/FOpenAiCompatibleHttpTransport.cpp`
- Set fast timeout: `HttpRequest->SetTimeout(15.0f)`.
- Added hard failure if `ProcessRequest()` returns false:
  - emits explicit error including URL.

### `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/FUnrealAiAgentHarness.cpp`
- Added progress log per LLM round submission.
- Added env override for round cap:
  - `UNREAL_AI_HARNESS_MAX_LLM_ROUNDS`
- Retry policy already tightened to avoid prolonged retry loops.

### `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/UnrealAiHarnessScenarioRunner.cpp`
- Added run-start log clarifying game-thread block behavior and log path.
- Reduced sync wait from 30 minutes to 60 seconds for debug iterations.

## Script behavior

### `scripts/run-headed-scenario-smoke.ps1`
- Live mode clears `UNREAL_AI_LLM_FIXTURE` if present.
- Uses env-var message file (`UNREAL_AI_HARNESS_MESSAGE_FILE`) and plain `UnrealAi.RunAgentTurn`.
- Uses comma command separator with `,Quit`.
- Supports `-MaxLlmRounds` (default now 2).

### `scripts/run-headed-live-scenarios.ps1`
- Live mode fixture-clearing behavior preserved.
- Uses env-var message file.
- Uses comma command separator with `,Quit`.
- Supports `-MaxLlmRounds` (default now 2).
- Uses ProcessStartInfo quoting path for robust launch.

### `scripts/run-headed-context-workflows.ps1`
- Also migrated to comma separator for command chaining.

---

## Confirmed Runtime Evidence (Real API)

From `Saved/Logs/blank.log` during latest runs:
- `LogTemp: Display: UnrealAi harness: LLM round 1/2 — submitting HTTP request (streaming).`
- `LogHttp: Warning: HTTP request timed out after 15.00 seconds URL=https://api.openai.com/v1/chat/completions`

From `Saved/UnrealAiEditor/HarnessRuns/.../run.jsonl`:
- `run_started`
- real tool activity (`asset_index_fuzzy_search`, `blueprint_export_ir`, etc.)
- terminal result, often capped by low round budget for fast looping.

Conclusion: the live API is definitely being called.

---

## Current Known Issues

## 1) Completion timing inconsistency

Observed:
- Log may show:
  - `UnrealAi.RunAgentTurn: completed=no ... err=Harness run timed out waiting for completion`
- But `run.jsonl` may still contain later tool events / `run_finished`.

Interpretation:
- Possible race or sequencing mismatch between:
  - sync wait completion in scenario runner,
  - sink completion signaling,
  - ongoing async event delivery.

Priority:
- High. This creates confusing pass/fail interpretation and user trust issues.

## 2) Tooling/prompt quality on Blueprint IR still weak

Observed in live run:
- repeated `blueprint_apply_ir` failures,
- non-existent assets referenced (e.g. `CoinSound`),
- invalid assumptions about available IR ops.

Interpretation:
- Prompt/tool contracts need tighter constraints.
- Model should be steered to:
  - discover existing assets first,
  - avoid unsupported IR patterns,
  - stop earlier and ask for missing data when necessary.

---

## Recommended Next Work (In Order)

## A) Fix completion race (first)
1. Ensure timeout path cancels active transport and prevents further event processing.
2. Ensure sink "finished" signal is emitted exactly once and respected as terminal.
3. In `RunAgentTurnSync_GameThread`, if timeout occurs:
   - call `Harness->CancelTurn()`,
   - wait briefly for terminal callback,
   - then return deterministic failure.
4. Add explicit log markers:
   - "sync_wait_timeout",
   - "sink_finish_received",
   - "runner_terminal_set".

## B) Add tiny deterministic regression test for timeout path
1. Use fixture or mock transport that never responds.
2. Verify:
   - no post-timeout tool events accepted,
   - run finishes once,
   - error is deterministic.

## C) Improve prompt/tool policy for Blueprint tasks
1. Add "capability constraints" to system/prompt:
   - avoid unsupported IR operations,
   - avoid fabricating assets/classes/functions.
2. Require a preflight:
   - asset search,
   - class/graph presence checks,
   - only then `blueprint_apply_ir`.
3. If preconditions fail:
   - return concise "blocked by missing asset/capability" and request next action.

---

## Commands That Worked Best

## Build
- `.\build-editor.ps1 -Headless -SkipBlueprintFormatterSync`
- If DLL lock issue:
  - `.\build-editor.ps1 -Restart -Headless -SkipBlueprintFormatterSync`

## Fast single live scenario
- `.\scripts\run-headed-live-scenarios.ps1 -SkipMatrix -MaxScenarios 1 -MaxLlmRounds 2`

## Fast smoke with live API
- `.\scripts\run-headed-scenario-smoke.ps1 -LiveApi -SkipCatalogMatrix -MaxLlmRounds 2`

## Force clean state before rerun
- `Get-Process UnrealEditor -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue`

---

## Where to Look During a Run

## Primary
- `Saved/Logs/blank.log`
- `Saved/UnrealAiEditor/HarnessRuns/<timestamp>/run.jsonl`

## Useful patterns
- `Cmd: UnrealAi.RunAgentTurn`
- `UnrealAi harness: LLM round`
- `HTTP request timed out`
- `RunAgentTurn: completed=...`

---

## Fast Restart Checklist (No Prior Context Needed)

1. Kill stale editors.
2. Build with `-Headless` (and `-Restart` if needed).
3. Run one scenario only:
   - `run-headed-live-scenarios.ps1 -SkipMatrix -MaxScenarios 1 -MaxLlmRounds 2`
4. Confirm in log that API URL is called.
5. Compare `blank.log` result vs `run.jsonl` terminal event.
6. If mismatch persists, focus on timeout cancellation + sink terminal ordering.
7. Only after completion behavior is deterministic, expand to prompt/tool quality tuning.

---

## Notes for Future Agent

- Do not trust viewport activity as progress indicator.
- Trust `blank.log` + `run.jsonl`.
- Keep iteration loops short (15s transport timeout, 60s sync wait, max rounds 2).
- If user asks "is API being called?", show direct log evidence with endpoint URL.
- Prefer one-scenario runs until completion semantics are rock solid.

