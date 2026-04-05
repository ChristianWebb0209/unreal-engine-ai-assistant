# Main-thread blocking diagnostics — logging and tests for editor hitches

This document plans a **local, opt-in observability layer** to answer: *which plugin work ran on the game thread, for how long, and in what order*, when the editor **freezes for multi-second intervals** while the agent runs.

**Related work (already planned or partially implemented):**

- Responsiveness plan (streaming parse + coalesced UI): [`agent-editor-responsiveness-streaming.md`](agent-editor-responsiveness-streaming.md).
- Harness stall hints (idle ages, sync timeout JSON): `UnrealAiHarnessProgressTelemetry` in `Plugins/UnrealAiEditor/.../Private/Misc/UnrealAiHarnessProgressTelemetry.*` — good for *why a wait timed out*, not yet a full *per-scope duration* timeline on the game thread.

**Non-goals:** shipping a remote telemetry backend; logging secrets, request bodies, or tool arguments (see repo privacy norms).

**Implementation (in-tree):** `Plugins/UnrealAiEditor/.../Private/Observability/UnrealAiGameThreadPerf.h|.cpp`, Editor Settings **Diagnostics → Game-thread perf scopes**, console `unrealai.GtPerf`, `UnrealAi.GtPerf.Dump` / `UnrealAi.GtPerf.Reset`, log category **LogUnrealAiPerf**.

---

## 1. Problem statement

Symptoms:

- Editor hitches (viewport/Slate unresponsive) for **~10 seconds** during agent activity.
- Causes may include: **large synchronous work on `ENamedThreads::GameThread`**, **queued `AsyncTask(GameThread, …)` storms**, **heavy Slate/layout**, **Blueprint or asset APIs**, **JSON/context assembly**, **SSE parse**, **tool dispatch**, or **interaction with other editor systems** on the same thread.

Without **time-bounded, labeled scopes**, debugging is guesswork: logs show *that* something happened, not *how long it blocked the main thread*.

---

## 2. Goals

1. **Detect** when plugin code (or clearly tagged call paths) holds the game thread longer than configurable thresholds (e.g. 16 ms, 100 ms, 1000 ms).
2. **Attribute** cost to a **stable scope name** (hierarchical: `Harness.DispatchLlm`, `Transport.ParseSse`, `ToolDispatch.blueprint_graph_patch`, `Context.BuildWindow`, `UI.TranscriptFlush`, etc.).
3. **Correlate** scopes with a **run id** / **turn id** / **tool invocation id** so multiple freezes in one session are separable.
4. **Support a diagnostic test** (automation or scripted harness) that reproduces agent traffic and produces a **machine-readable timeline** (JSON lines or CSV) for diffing before/after changes.

---

## 3. Design principles

- **Opt-in:** default overhead near zero; enable via project/plugin setting or cvar.
- **Game-thread accurate:** measure **CPU time while the game thread is inside the scope**, not wall clock on worker threads (unless explicitly labeled as async producer latency).
- **Low cardinality:** scope names are fixed strings; **no unbounded strings** (asset paths, prompts) in hot paths — use hashes or “first N chars” only in verbose tiers.
- **Ring buffer + flush:** keep the last *N* slow events in memory; optional **dump to file** on hitch (e.g. any scope longer than one second) or on demand from a console command.
- **Composable with engine tools:** Unreal Insights, CSV profiler, and `STAT` groups remain first-class; this layer is for **plugin-specific attribution** when Insights is too coarse or too heavy for daily use.

---

## 4. Proposed subsystem (conceptual)

### 4.1 `FUnrealAiGameThreadScope` (RAII)

- Constructor records `FPlatformTime::Seconds()` (or `FPlatformTime::Cycles()` with cycle stats) and **scope id** / **parent scope** (thread-local stack).
- Destructor computes **elapsed**; if elapsed ≥ threshold, **record** `SlowScopeRecord` { timestamp, duration_ms, scope_name, parent_name, correlation_ids, optional byte counters }.
- Use **UE_LOG category** `LogUnrealAiPerf` at **VeryVerbose** for every scope (optional), and **Log** only for **over-threshold** events to avoid spam.

### 4.2 Correlation model

Minimal fields (extend as needed):

- `RunId` — incremented per harness “run” or editor session attach.
- `TurnSeq` — monotonic per chat turn.
- `ToolCallId` / tool name — when inside tool execution (already partially aligned with harness tool lifecycle).

### 4.3 Aggregation

- **Per-tick** optional: sum of plugin time in `FTSTicker` callback vs total tick (requires careful boundary definition).
- **Histograms** in memory for top scopes (optional dev-only).

### 4.4 Console / API surface

- `unrealai.perf.dump` — write last *K* slow records to `Saved/UnrealAiPerf/…`.
- `unrealai.perf.reset` — clear buffers.
- Settings: thresholds, enable flag, max buffer size, whether to include child scopes in parent totals.

---

## 5. Instrumentation map (initial targets)

Prioritize scopes that are known or suspected to run on the game thread during agent activity:

| Area | Example scopes | Notes |
|------|------------------|--------|
| Harness | `Harness.DispatchLlm`, `Harness.HandleEvent`, `Harness.ToolRound` | Pair with stream coalescer flush boundaries |
| Transport | `Transport.StreamParse`, `Transport.OnComplete` | Align with incremental vs full-body parse |
| Context | `Context.BuildContextWindow`, `Context.RankCandidates` | Large string work |
| Tool surface | `ToolSurface.Pipeline`, `ToolSurface.BM25` | Already has some latency fields in structured telemetry — unify naming |
| Tool dispatch | `ToolDispatch.Execute`, per logical tool bucket | One layer above individual tools to keep cardinality low |
| UI | `UI.CoalescerFlush`, `UI.TranscriptUpdate`, `SToolCallCard` refresh | Watch for layout thrash |

Refine names during implementation so they match **one stable taxonomy** (documented in code as comments or a single enum/string table).

---

## 6. Diagnostic test strategy

### 6.1 Repro

- Use existing **headless harness** or a **fixed scenario** (deterministic tool loop + streaming mock if available) so freezes are reproducible across builds.
- Optional: **record** `stat startfile` / **Unreal Insights** trace for the same scenario in parallel with plugin scope logs (manual step documented in test README).

### 6.2 Assertions (soft)

- Fail or warn if any single scope **exceeds budget** (e.g. 2000 ms) during the scenario, or if **sum of plugin time per simulated second** exceeds a ceiling.
- Keep thresholds **generous** on CI to avoid flakes; tighten locally when hunting regressions.

### 6.3 Output artifact

- JSON Lines: one object per slow scope, easy to **sort by `duration_ms`** and **group by `scope`** in a script.
- Include **engine version**, **build configuration**, and **git hash** (if available) as header record for comparison runs.

---

## 7. Rollout phases

| Phase | Deliverable |
|-------|-------------|
| **P0** | RAII scope + ring buffer + thresholded UE_LOG; console dump command; document enablement in plugin settings |
| **P1** | Wire top 10–15 scopes across harness, transport, context build, tool dispatch entry |
| **P2** | Automation test + golden “no catastrophic scope” check; optional JSON export |
| **P3** | Optional integration: mirror slow scopes into `UnrealAiHarnessProgressTelemetry` diagnostic JSON for single-line correlation with existing idle-abort messages |

---

## 8. Success criteria

- With perf logging enabled, a **10 s freeze** produces at least one **attributed scope** (or a clear gap indicating non-plugin game-thread work).
- Two runs of the same scenario yield **consistent top offenders** (same scope family), enabling before/after verification when applying [`agent-editor-responsiveness-streaming.md`](agent-editor-responsiveness-streaming.md) or other fixes.
- Default **off** path: no measurable impact on shipping builds (or impact bounded by a documented micro-benchmark).

---

## 9. References (Unreal Engine)

- **Unreal Insights** — thread timeline and CPU scopes (engine-level).
- **CSV Profiler** (`stat startfile` / `stat stopfile`) — quick capture of frame time.
- **`SCOPE_CYCLE_COUNTER` / `DECLARE_CYCLE_STAT`** — fine-grained stats when registered in the plugin module.
- **`FPlatformTime`** — lightweight duration checks inside RAII scopes.

Use engine documentation for exact macros and command syntax for the UE version you target (this repo assumes **UE 5.7** per project rules).
