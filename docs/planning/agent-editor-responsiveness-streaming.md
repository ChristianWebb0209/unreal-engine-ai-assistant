# Agent editor responsiveness — incremental HTTP streaming and coalesced UI updates

This document plans work to stop **whole-editor hitching** while the agent runs: long LLM streams today buffer until the HTTP request completes, then parse the full SSE body and fan out many game-thread tasks. We adopt two coordinated strategies (see competitive notes in [reference/arch-analysis.md](../../reference/arch-analysis.md) and internal comparison in [reference/ludus/ARCHITECTURE_COMPARISON_LUDUS_VS_UNREAL_AI_EDITOR.md](../../reference/ludus/ARCHITECTURE_COMPARISON_LUDUS_VS_UNREAL_AI_EDITOR.md)):

1. **Incremental HTTP streaming** — parse SSE as bytes arrive (Autonomix-style `OnRequestProgress64`), not after `OnProcessRequestComplete`.
2. **Coalesced UI updates** — batch assistant/thinking deltas on the game thread (or in a thread-safe buffer flushed by `FTSTicker`) instead of one `AsyncTask(ENamedThreads::GameThread, …)` per stream event.

**Primary code today:** `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Transport/FOpenAiCompatibleHttpTransport.cpp` (full-body `ParseSseBody` after complete), `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/FUnrealAiAgentHarness.cpp` (`DispatchLlm` → per-event `AsyncTask` to `HandleEvent`).

**Non-goals (this phase):** moving Blueprint/tool execution off the game thread (engine APIs require GT); optional background JSON parse of entire bodies without incremental I/O (incremental streaming supersedes that for chat).

---

## 1. Problem statement

- `FOpenAiCompatibleHttpTransport::StreamChatCompletion` waits for **`OnProcessRequestComplete`**, then runs **`OpenAiTransportUtil::ParseSseBody(RespBody, OnEvent)`** on the full string. Large streams mean **large single-frame parse + callback storm**.
- The harness binds the transport callback with **`AsyncTask(ENamedThreads::GameThread, …)`** per event, which can **queue thousands of tasks** for a single completion.
- Chained tools already **yield between tools** via `FTSTicker` in headed mode; the remaining pain is **LLM receive/parse/UI fanout**, not only tool loops.

---

## 2. Strategy A — incremental HTTP streaming

### 2.1 Target behavior

- Bind **`IHttpRequest::OnRequestProgress64`** (or equivalent) on the same request used for chat completions when `Request.bStream` is true.
- Maintain **per-request state**: raw byte offset or `LastBytesReceived`, an **SSE line accumulator** (`FString` or `TArray<uint8>` with UTF-8-safe conversion only on complete lines — mirror Autonomix `SSELineBuffer` / `HandleRequestProgress` pattern in `reference/autonomix/Autonomix-main/Source/AutonomixLLM/Private/AutonomixOpenAICompatClient.cpp`).
- On each progress callback, process **only new bytes**, split on `\n`, feed complete `data: …` lines into the same logical handlers as today’s `ParseSseBody` (reuse extraction into `FUnrealAiLlmStreamEvent` to avoid drift).
- On **`OnProcessRequestComplete`**, **flush** any trailing partial line (documented pitfall in Autonomix: do not re-parse the full body if progress already consumed it).
- **429 retry path:** today sleeps on the completing thread; after refactor, ensure **active request / buffer state** is reset consistently per attempt (no double emission of deltas).

### 2.2 Threading contract

- UE’s HTTP delegates may run on the **game thread** or a **HTTP thread** depending on module configuration; **do not assume**. Any call that eventually touches **`FUnrealAiLlmStreamCallback` / UObject / Slate** must go through the **coalescing layer** (Strategy B) that is safe from non-game threads (lock-free or `FCriticalSection` + atomic “pending flush” flag, flush only on game thread).

### 2.3 Implementation steps

1. **Extract** SSE line → events logic from monolithic `ParseSseBody` into reusable functions (e.g. “append UTF-8 chunk to buffer”, “drain complete lines”, “parse one `data:` JSON line into zero or more `FUnrealAiLlmStreamEvent`”) so both **full-body** (non-streaming / tests) and **incremental** paths share one parser.
2. **Add** a small **stream session** struct (buffer, `LastBytesReceived`, `LastFinishReason`, usage accumulation) owned by the transport or a dedicated helper class; clear it on new request and on cancel.
3. **Wire** `OnRequestProgress64` when `bStream` is true; implement **incremental** processing mirroring Autonomix offset tracking against `Request->GetResponse()->GetContent()`.
4. **Adjust** `OnProcessRequestComplete` for streaming: **only** final flush + error handling + telemetry (`NotifyHttpStreamParseComplete`), not full `ParseSseBody(RespBody)` when incremental path already ran.
5. **Add** automated or manual test notes: hit a long streaming completion (or mock HTTP with chunked content) and confirm **no single multi-second stall** at complete; verify **tool call deltas** still merge correctly across chunks.
6. **Document** in plugin README or tooling doc one paragraph: “streaming is incremental; cancel aborts in-flight buffer.”

---

## 3. Strategy B — coalesced UI / harness updates

### 3.1 Target behavior

- **Producers** (HTTP thread or game thread) push **logical events** or **text deltas** into a **thread-safe queue or ring buffer** (or coalesce into “pending assistant text”, “pending thinking text”, “pending tool-call merge state” with a mutex).
- **Consumer** runs on the **game thread** at a capped rate (e.g. **`FTSTicker` every 16–33 ms** or flush when queue depth exceeds N) and calls **`FAgentTurnRunner::HandleEvent`** (or narrower methods: `ApplyAssistantDelta`, `ApplyThinkingDelta`, `MergeToolCalls`) **once per tick** with **batched** payloads.
- **Preserve semantics:** tool-call streaming merge (`MergeToolCallDeltas`, `EnqueueNewlyCompleteCalls`, `StartOrContinueStreamedToolExecution`) must see **the same final argument strings** as today; batching must not reorder **tool call index** updates incorrectly. Prefer **batching assistant/thinking text** first (low risk), then **batch tool-call JSON fragments** only if merge order is proven identical to per-chunk handling.
- **Cancel / terminal:** flush pending buffered text immediately on `bCancelled` / `bTerminal` so the UI does not lose the tail.

### 3.2 Implementation steps

1. **Introduce** `FUnrealAiLlmStreamCoalescer` (or similar) in `Private/Harness/` or `Private/Transport/`:
   - `Enqueue(const FUnrealAiLlmStreamEvent&)` or specialized enqueue by type;
   - `PumpFlush()` called from ticker on game thread;
   - configurable **max interval** and **max bytes per flush**.
2. **Change** `FAgentTurnRunner::DispatchLlm` so the transport callback **does not** `AsyncTask(GameThread, …)` per event; it calls **`Coalescer->Enqueue`** (or transport writes directly into coalescer if parsing off-thread).
3. **Register** a **single** `FTSTicker` handle per active turn (or per transport session) for flush; **remove** ticker on turn end / cancel; guard against double-registration.
4. **Handle `Finish` and `Error` events** as **high priority**: bypass coalescing delay or flush synchronously before processing terminal events so harness state machine does not reorder finish before last deltas.
5. **Telemetry:** optional counters (`stream_events_enqueued`, `stream_flushes`, `coalesced_chars`) behind verbose logging for validation.
6. **Regression:** run existing harness / catalog tests; manually verify Slate transcript still updates smoothly under fast streaming models.

---

## 4. Rollout order

| Phase | Work | Rationale |
|--------|------|------------|
| **1** | Strategy B (coalescer + ticker flush) while keeping **full-body** parse at complete | Reduces callback storm **immediately** with smaller transport change. |
| **2** | Strategy A (incremental progress + flush on complete) | Removes huge `ParseSseBody` on complete; pairs with coalescer for safe cross-thread enqueue. |

If incremental streaming lands first without coalescing, progress callbacks could still **spam** the game thread — so **coalescing should follow immediately** or ship in the same PR series.

---

## 5. Success criteria

- While a long assistant reply streams, **editor remains interactive** (viewport/Slate usable; no multi-second contiguous freeze at stream end).
- **Tool calls** from streamed JSON behave identically to pre-change behavior (same merge and execution order).
- **Cancel** during stream leaves no dangling ticker and no use-after-free on the runner.
- **Headless harness** / scenario sync modes: either unchanged behavior or explicitly gated (coalescer flush interval 0 = immediate) so automation does not time out.

---

## 6. Files likely touched

- `Plugins/UnrealAiEditor/.../Transport/FOpenAiCompatibleHttpTransport.cpp` (+ possibly `.h`)
- `Plugins/UnrealAiEditor/.../Harness/FUnrealAiAgentHarness.cpp`
- New: `Private/Harness/UnrealAiLlmStreamCoalescer.h/.cpp` (name TBD)
- `Plugins/UnrealAiEditor/.../Harness/ILlmTransport.h` or callback typedefs only if the coalescer becomes part of the transport interface
- Tests under `Plugins/UnrealAiEditor/.../Tests/` or scripted harness if HTTP is hard to unit-test (extract parser to pure functions for `RUN_TEST` without network)

---

## 7. References

- Autonomix incremental stream handling: `reference/autonomix/Autonomix-main/Source/AutonomixLLM/Private/AutonomixOpenAICompatClient.cpp` (`HandleRequestProgress`, `ProcessSSEChunk`, complete-handler flush comment).
- UE LLM Toolkit threading model: `reference/ue-llm-toolkit/.../ClaudeCodeRunner.cpp` (`FRunnable` + `AsyncTask(GameThread, …)`).
- Internal harness yield between tools: `FUnrealAiAgentHarness.cpp` (`StartOrContinueStreamedToolExecution`).
