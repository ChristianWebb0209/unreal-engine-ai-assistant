## PIE lifecycle strict suite — context handoff

This file captures the current state of work on PIE lifecycle tooling, strict suites, and related crashes/stalls, so a future agent (or human) can resume with full context instead of rediscovering the same issues.

---

### High-level goals

- **Strict PIE lifecycle coverage**: We want a strict suite (`strict_catalog_runtime_render_gap_v1`) that reliably tests:
  - Starting PIE from a clean editor session.
  - Stopping PIE and confirming the editor’s PIE flags reflect a fully torn-down session.
- **Deterministic harness behavior**: Headed harness + strict suites should:
  - Avoid editor crashes and assertion failures (notably TaskGraph `RecursionGuard`).
  - Avoid stalls where the harness sits at `Harness turns N/M` indefinitely.
  - Produce clear `run.jsonl` / `strict_assertions_result.json` artifacts that distinguish “tool contract failure” from “infrastructure/harness failure”.
- **Realistic prompts**: User-facing `request` strings in suites must:
  - Read like real editor users.
  - Avoid direct references to internal tool ids or file paths.
  - Use viewport capture only as a *last resort* when visual evidence is required, not the default way to check PIE state.

---

### Current strict suite state (`strict_catalog_runtime_render_gap_v1`)

- **Location**: `tests/strict-tests/suites/strict_catalog_runtime_render_gap_v1.json`.
- **Intent**: Covers a small but strict scenario around runtime/PIE lifecycle and related tools:
  - `t01_agent_enter_preview`: agent should start PIE in a reasonable way from the current level.
  - `t02_agent_exit_preview`: agent should stop PIE and confirm the editor is no longer in a playing state.
- **Pruning history**:
  - Suite originally had more turns testing:
    - `collision_trace_editor_world` line probes.
    - `viewport_capture_delayed` behavior and file outputs.
    - Additional PIE/viewport interactions.
  - Over several iterations we **removed turns that consistently passed** so that reruns focus only on problematic behaviors:
    - Removed original turns `t04`–`t07`.
    - Later removed turns that focused on world line probes and viewport capture once those were stable.
  - Today, the suite is **reduced to just two turns** (`t01`, `t02`) to isolate PIE start/stop.
- **Last run status**:
  - Command: `.\tests\strict-tests\run-strict-headed.ps1 -Suite "strict_catalog_runtime_render_gap_v1" -MaxSuites 1`.
  - Latest run was **paused mid-stall**:
    - Terminal showed `Harness turns 0/2 | strict 0/2` for >10 minutes.
    - This suggests a hang early in `t01_agent_enter_preview` (PIE start path or very first harness operations).
  - We explicitly **killed** `UnrealEditor.exe` and `CrashReportClientEditor.exe` to stop that run and avoid DLL locks.

---

### Key tools and code involved

#### PIE tools (`UnrealAiToolDispatch_Pie.cpp`)

- File: `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_Pie.cpp`.
- Tools of interest:
  - `pie_start` (not heavily modified in this investigation but participates in stalls).
  - `pie_stop` (heavily iterated to try to make teardown observable and deterministic).
  - `pie_status` (used in strict assertions to check `playing_in_editor` and related flags).
- Important Unreal APIs in play:
  - `GEditor->RequestPlaySession()` / `GEditor->RequestEndPlayMap()` (asynchronous).
  - `GEditor->EndPlayMap()` (synchronous but unsafe to call from arbitrary tool contexts).
  - PIE-related flags:
    - `GEditor->IsPlayingSessionInEditor()`.
    - `GEditor->IsPlaySessionInProgress()`.
    - `GEditor->IsPlaySessionRequestQueued()`.
  - Game-thread pumping mechanisms:
    - `FHttpModule::Get().GetHttpManager().Tick(0.f)`.
    - `FTaskGraphInterface::Get().ProcessThreadUntilIdle(...)` (dangerous due to `RecursionGuard`).
    - `GEditor->Tick(float DeltaTime, bool bIdleMode)` (must be bounded carefully).
    - `FPlatformProcess::SleepNoStats(seconds)` (used between ticks).

#### Viewport tools (`UnrealAiToolDispatch_Viewport.cpp`)

- File: `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_Viewport.cpp`.
- Tools of interest:
  - `viewport_capture_png` — immediate screenshot capture.
  - `viewport_capture_delayed` — originally a no-op wrapper around PNG capture; now implemented to tick for `delay_frames` before capturing.
- Supporting includes:
  - `#include "HttpModule.h"`, `#include "HttpManager.h"` for `FHttpModule::Get().GetHttpManager().Tick()`.
  - `#include "HAL/PlatformProcess.h"` for sleep.

---

### Iteration history on `pie_stop`

This is the most sensitive area; multiple iterations were attempted to make `pie_stop` both fast and reliable.

#### Initial behavior

- `pie_stop` was essentially:
  - Calling `GEditor->RequestEndPlayMap()` (asynchronous).
  - Returning quickly with success + some flags.
- Problem:
  - Immediate `pie_status` calls in strict tests sometimes observed:
    - `playing_in_editor == true`
    - `play_session_in_progress == true`
  - even though `pie_stop` had just returned `ok`.
  - This caused flaky strict assertions; teardown often finished *slightly later*.

#### Attempt 1 — `ProcessThreadUntilIdle(GameThread)` (crash via `RecursionGuard`)

- Strategy:
  - After `RequestEndPlayMap()`, loop until flags cleared or timeout (~5s).
  - Each iteration:
    - `FHttpModule::Get().GetHttpManager().Tick(0.f);`
    - `FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);`
    - Short sleep.
- Result:
  - Editor crashed at runtime with:
    - Assertion: `++Queue(QueueIndex).RecursionGuard == 1` inside `TaskGraph.cpp`.
  - Call stack showed `UnrealAiDispatch_PieStop` calling into `ProcessThreadUntilIdle`, while the harness was *already* using `WaitForHarnessEventWhilePumpingGameThread`.
- Takeaway:
  - **Do not call `ProcessThreadUntilIdle(GameThread)` from inside tool code**; it violates the TaskGraph recursion guard because the harness is already pumping the game thread.

#### Attempt 2 — `GEditor->Tick(0.01f, false)` in loop (stall)

- Strategy:
  - Replace `ProcessThreadUntilIdle` with `GEditor->Tick(0.01f, false)` in a loop.
  - Still call `FHttpModule::Tick` and sleep, but rely on `GEditor->Tick` to process the end-play request.
- Result:
  - Harness stalled; run sat at `Harness turns 1/4 | strict 1/4`.
  - `GEditor->Tick(..., false)` from this context appears to be too heavy/risky; may re-enter game-thread-y work in ways that block harness progress.
- Takeaway:
  - **Avoid non-idle `GEditor->Tick` from inside tools**; can still create re-entrancy/ordering issues.

#### Attempt 3–4 — Sleep-only / longer deadlines (still wrong flags)

- Strategy:
  - Remove `GEditor->Tick` entirely; after `RequestEndPlayMap()` just:
    - Sleep in a loop.
    - Check flags until a deadline (increased from 5s → 20s).
  - Rely on harness-level game-thread pumping (which happens while waiting for tools) to process the queued end-play.
- Result:
  - No crash, but still saw stale PIE flags in strict assertions:
    - `IsPlayingSessionInEditor()` and `IsPlaySessionInProgress()` sometimes stayed `true` even after waiting.
  - Implied that harness pumping alone wasn’t enough to guarantee teardown completed before returning from `pie_stop`.
- Takeaway:
  - Sleep-only loops are **not sufficient** to guarantee PIE teardown visibility under all conditions.

#### Attempt 5 — Direct `EndPlayMap()` (unsafe)

- Strategy:
  - Call `GEditor->EndPlayMap()` directly inside `pie_stop` to force synchronous teardown.
- Result:
  - Build + run revealed this is **unsafe** in this context:
    - Harness stall / incomplete batch.
    - Editor exit code `3` in suite summary.
  - This aligned with UE’s expected usage; `EndPlayMap()` is intended for use from the main editor tick, not arbitrary tool invocation stacks.
- Takeaway:
  - **Do not call `EndPlayMap()` from tools**; keep using `RequestEndPlayMap()` and rely on safe ticking contexts.

#### Attempt 6 — `ProcessThreadUntilIdle(GameThread_Local)` (stubborn flags)

- Strategy:
  - Go back to TaskGraph pumping, but use `ENamedThreads::GameThread_Local` hoping to avoid RecursionGuard issues.
  - Still tick HTTP manager, sleep, wait up to ~15s.
- Result:
  - No crash, but strict assertions still observed PIE flags remaining `true` after `pie_stop`.
  - So we were pumping *something*, but not enough of what mattered, or at the wrong time/queue.
- Takeaway:
  - `GameThread_Local` pumping is not a reliable substitute for the main game-thread pump from this context.

#### Attempt 7 — Bounded `GEditor->Tick(0.f, true /*bIdleMode*/)` loop (current code at time of pause)

- Strategy:
  - Compromise approach:
    - Call `RequestEndPlayMap()`.
    - For up to 50 iterations and up to 10s:
      - If all flags (`IsPlayingSessionInEditor`, `IsPlaySessionInProgress`, `IsPlaySessionRequestQueued`) indicate “not playing,” break early.
      - Otherwise:
        - `FHttpModule::Get().GetHttpManager().Tick(0.f);`
        - `GEditor->Tick(0.f, true /*bIdleMode*/);`
        - `FPlatformProcess::SleepNoStats(0.001f);`
  - Use `bIdleMode=true` to reduce side effects vs full editor tick.
- Result:
  - Builds and runs, but the **latest strict suite run stalled earlier**, before we could verify whether PIE flags behaved better:
    - Stall at `Harness turns 0/2 | strict 0/2` suggests the stall is in PIE start or initial harness operations, not yet in `pie_stop`.
  - We paused investigation here (killed the editor, wrote this context).
- Takeaways:
  - This approach **may still be too invasive**, especially when combined with harness-level pumping.
  - We have not yet conclusively validated whether this version of `pie_stop` fully satisfies the strict assertions without causing stalls elsewhere.

---

### Iteration history on `viewport_capture_delayed`

#### Original behavior

- `UnrealAiDispatch_ViewportCaptureDelayed` simply forwarded to `UnrealAiDispatch_ViewportCapturePng` and ignored the `delay_frames` argument.
- Consequences:
  - Strict tests that relied on it as a frame-pumping mechanism were mis-specified; they expected delay but got none.
  - It wasn’t actually helping PIE teardown or visual stabilization.

#### Fix — implement delay via ticking

- Implemented `delay_frames` properly:
  - Clamp `DelayFrames` to `>= 0`.
  - If `DelayFrames > 0 && GEditor`:
    - For each frame:
      - `FHttpModule::Get().GetHttpManager().Tick(0.f);`
      - `GEditor->Tick(0.f, true /*bIdleMode*/);`
      - `FPlatformProcess::SleepNoStats(0.001f);`
  - After the loop, call `UnrealAiDispatch_ViewportCapturePng(Args)`.
- Build issues:
  - Needed explicit include of `HttpManager.h` (forward declaration wasn’t enough for `GetHttpManager()` usage).
  - Once included, code compiled and acted as intended.

#### Relationship to PIE status checks

- At one point, we tried to use `viewport_capture_delayed` as a way to:
  - Pump the editor for a few frames after `pie_stop`.
  - Then assert on `pie_status`.
- Problems:
  - This makes viewport capture a *de facto* core part of PIE lifecycle checking, which is **not desirable**:
    - It’s heavier than necessary.
    - Ties visual tooling to lifecycle correctness.
  - The agent did not consistently choose `viewport_capture_delayed` without explicitly being told to do so, which made prompts brittle.
- Current stance (and user preference):
  - **Viewport capture should be a last resort**, used mainly for visual questions.
  - PIE lifecycle tests should rely primarily on `pie_start`, `pie_stop`, `pie_status`, and log/flag inspections, not on screenshots.

---

### Harness/test-level artifacts and changes

#### Time-summary artifacts

- File: `tests/qualitative-tests/run-qualitative-headed.ps1`.
- Added functionality:
  - `Get-RunJsonlModelWallDuration`:
    - Parses `run.jsonl` for each turn.
    - Derives per-turn wallclock durations from:
      - `harness_sync_wait_start` / `harness_sync_segment_wait_start` (preferred).
      - `harness_sync_wait_finished` / `harness_sync_segment_result`.
      - Fallback to `context_build_timing.utc_start` if necessary.
  - `Write-BatchTurnTimeSummary`:
    - Writes `turn-time-summary.json` and `turn-time-summary.md` into each batch output folder.
    - Provides a quick view of how long the model took per turn, or `N/A` if not run.

#### Tool iteration log

- File: `tests/tool-iteration-log.md`.
- Recent entries related to this work:
  - **Entry 40**:
    - Logs early `pie_stop` fix attempts and corresponding strict suite updates (e.g., requiring all PIE flags to be false after `pie_stop`; adding viewport delay tests).
  - **Entry 41–43**:
    - Capture broader harness and tool coverage analysis.
  - **Entry 44**:
    - Focuses on UI changes to show `unreal_ai_dispatch` tool cards more clearly in chat and moves some “easy pass” qualitative turns into `passed-tests.json`.
- This log is the right place to add **future notes** about further `pie_stop`/PIE lifecycle fixes and strict suite changes.

#### To-do tracking (`docs/todo.md`)

- File: `docs/todo.md`.
- Under **Release Readiness Testing** we now have:
  - An item for **Strict PIE lifecycle**:
    - Mentions `strict_catalog_runtime_render_gap_v1`.
    - Notes that past runs show `pie_stop` sometimes leaves PIE flags `true`.
    - Notes that the latest attempt was paused after a stall at `Harness turns 0/2`.
    - Reminds that we should re-evaluate PIE start/stop prompts and treat viewport capture as a last resort.

---

### Known failure modes and lessons learned

1. **TaskGraph `RecursionGuard` crash**
   - Cause:
     - Calling `FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread)` from inside `UnrealAiDispatch_PieStop`.
     - The harness already uses `WaitForHarnessEventWhilePumpingGameThread`, so this double-pumped the GameThread queue.
   - Fix/lesson:
     - **Never call `ProcessThreadUntilIdle(GameThread)` from tools**.
     - Prefer leaning on harness-level pumps or minimal `GEditor->Tick(..., true)` only when strictly necessary and well-bounded.

2. **Stalls after aggressive `GEditor->Tick`**
   - Cause:
     - Calling `GEditor->Tick(0.01f, false)` in a tight loop inside `pie_stop`.
   - Symptom:
     - Harness stuck at `Harness turns 1/4 | strict 1/4` in earlier versions of the suite.
   - Lesson:
     - Full editor ticks from tool context are risky; can entangle tool work with the editor’s main runloop and stall progress.

3. **Sleep-only wait loops insufficient**
   - Cause:
     - Only sleeping and checking flags after `RequestEndPlayMap()`, relying solely on harness pumping.
   - Symptom:
     - PIE flags sometimes remained `true` even after reaching long deadlines.
   - Lesson:
     - There is no guarantee that harness-level game-thread work alone will flush all relevant teardown state within a fixed time, especially under load.

4. **Direct `EndPlayMap()` is unsafe**
   - Cause:
     - Calling `GEditor->EndPlayMap()` directly from `pie_stop`.
   - Symptom:
     - Incomplete batches with editor exit code `3`.
   - Lesson:
     - End-play must be orchestrated via the usual engine tick path (`RequestEndPlayMap()` + tick), not from arbitrary plugin/tool stacks.

5. **Using viewport capture as lifecycle oracle is brittle**
   - Cause:
     - Relying on `viewport_capture_delayed` to implicitly pump frames and then checking PIE state.
   - Symptoms:
     - Agent not always choosing the viewport tool when not explicitly instructed.
     - Tight coupling between screenshot tooling and lifecycle correctness.
   - Lesson:
     - Viewport capture should be **last resort**, mainly for visual confirmation questions.
     - PIE lifecycle checks should use dedicated lifecycle/status tools.

6. **Repeated reruns without pruning waste time**
   - Cause:
     - Re-running full suites without trimming turns that always pass.
   - Lesson:
     - For long-running headed strict/qualitative suites, **prune easy passes between reruns** so iteration time focuses on problematic behaviors.

---

### Brainstorm: future hardening directions

These are ideas for making the system more robust, beyond “bump timeouts”.

1. **Redefine `pie_stop` contract**
   - Instead of “must fully stop PIE before returning”, define it as:
     - “Issue a stop request; return current flags and whether a stop was *requested successfully*.”
   - Let the harness or the agent:
     - Poll `pie_status` with existing, well-tested harness pumping.
   - This avoids deep, bespoke pump loops inside `pie_stop`.

2. **Introduce a small PIE lifecycle state machine**
   - Add internal state (e.g., `Idle`, `Starting`, `Running`, `Stopping`, `Stopped`, `Error`) managed in game-thread tick or a central manager.
   - Tools like `pie_start` / `pie_stop`:
     - Request transitions instead of directly performing work.
   - Strict tests assert on **state transitions** and **flags** rather than requiring tools to do heavy synchronous work.

3. **Add reentrancy and recursion guards for tools**
   - For risky tools (PIE, viewport, large frame-pumping operations):
     - Add static or member flags guarded by scope (`TGuardValue<bool>`) to prevent recursive execution.
   - On re-entry:
     - Return structured errors rather than proceeding.

4. **Constrain editor ticking from tools**
   - If `GEditor->Tick` must be used from a tool:
     - Always use `bIdleMode=true`.
     - Enforce strict bounds:
       - Max number of ticks per call.
       - Max real time budget per tool invocation.
     - Document and centralize this logic so that future tools don’t introduce new tick loops ad hoc.

5. **Separate normal vs diagnostic tools**
   - Keep **fast-path** tools that:
     - Never do heavy ticking.
     - Are safe for everyday chat usage.
   - Add **diagnostic variants** (only used in strict suites or debug scenarios) that:
     - May tick a bit more.
     - Provide verbose diagnostics (e.g., per-flag histories, timings, attempt counts).

6. **Improve harness error reporting and auto-recovery**
   - When the harness detects:
     - No new events for a long time.
     - Editor exit codes indicating crashes.
   - It should:
     - Emit explicit “infrastructure error” events into `run.jsonl`.
     - Mark suites as incomplete with a clear message.
     - Optionally restart the editor between suites when safe.

7. **Explicit prompt guidance about viewport tools**
   - For any strict/qualitative suite dealing with PIE:
     - Ensure `request` text:
       - Suggests status/check tools first.
       - Only hints at screenshots when visual evidence is truly needed.
   - This keeps test expectations in line with the desired tool hierarchy (status tools first, viewport capture last).

---

### Where we paused

- We stopped active work at a point where:
  - `pie_stop` uses a bounded loop with `FHttpModule::Tick`, `GEditor->Tick(0.f, true)`, and sleep after `RequestEndPlayMap()`.
  - `viewport_capture_delayed` now correctly implements `delay_frames` ticking.
  - `strict_catalog_runtime_render_gap_v1` is trimmed to two turns and is **currently failing to progress** (stall at `Harness turns 0/2`) on the latest run.
  - Editor and crash reporter processes were forcibly terminated to clear the stall.
  - `docs/todo.md` logs that:
    - Strict PIE lifecycle needs more work.
    - We should revisit prompts (viewport capture as last resort).
    - We need further crash-proofing before the next run.

---

### Suggested next steps for the next agent

1. **Re-run strict PIE suite after a small contract change**
   - Consider first redefining `pie_stop` to:
     - Only request end-play and return current flags (no internal tick loop).
   - Then adjust `strict_catalog_runtime_render_gap_v1` to:
     - Allow for an intermediate state where `pie_status` may initially say “still playing”.
     - Expect the *agent* to poll `pie_status` until it observes the stopped state (within some bound).

2. **Instrument harness for better stall diagnostics**
   - Add more detailed logging around:
     - When `RunAgentTurnSync` starts/finishes.
     - Which tool call we’re waiting on when we decide to time out.
   - Use this to pinpoint exactly where the `Harness turns 0/2` stall is occurring (PIE start vs something earlier).

3. **Re-evaluate where game-thread pumping should live**
   - Prefer a single “owner” of game-thread pumping (likely the harness).
   - Strip as much custom pumping as possible from tools (`pie_stop`, `viewport_capture_delayed`) and see if we can rely primarily on harness behavior.

4. **Update `tests/tool-iteration-log.md` when you make changes**
   - Add a new entry (next number after the current highest) describing:
     - Changes to `pie_stop` contract/implementation.
     - Any further modifications to strict PIE suites or prompts.
     - Observed behavior from subsequent runs.

