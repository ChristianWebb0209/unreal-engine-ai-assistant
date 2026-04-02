# Agent log analysis guide

Quick reference for **where UnrealAiEditor writes artifacts** under a UE project and **how to answer debugging questions** from those files. Paths are relative to the **project’s `Saved/`** directory unless noted.

**Base plugin folder:** `Saved/UnrealAiEditor/`

---

## 1. Harness / automation runs (richest single place)

**Directory:** `Saved/UnrealAiEditor/HarnessRuns/<UTC_YYYYMMDD_HHMMSS>/`  
(Override: pass a custom output dir to `RunAgentTurn` / scenario runner.)

| File | What it contains | Use it to answer |
|------|------------------|------------------|
| `run.jsonl` | One JSON object per line: `run_started`, `context_user_messages`, `assistant_delta`, `thinking_delta`, `tool_start` (args), `tool_finish` (`success`, `result_preview`), `continuation`, `planning_decision`, `enforcement_event` / `enforcement_summary`, `run_finished`, etc. | **Which tools ran, arguments, success/failure, streamed model text**, phase/continuation, policy events. Internal ranks: look for `enforcement_event` with types like `tool_selector_ranks` / `tool_surface_*` (full detail may be large). |
| `llm_requests.jsonl` | **Outbound** HTTP chat payload per round: `messages` (array), `tools` (array), model id, round index, thread/run ids. API key is **not** written; `api_key_redacted` is boolean. | **Exact system/user context and tool definitions** sent to the provider for that round. |
| `harness_progress.log` | Plain text, timestamped lines; written even if `run.jsonl` stalls. | **Whether the run progressed**, HTTP/tool milestones, hangs. |
| `context_window_<reason>.txt` | Full **built context block** text at dump points (`run_started`, after each tool, `continuation_*`, optional `run_finished`). | **What the context service would inject** into the model payload (align with `messages` in `llm_requests.jsonl`). |
| `context_window_<reason>_verbose.txt` | Optional; when harness verbose context env is on (see §5). | Extra **internal trace** lines from context build. |

**Caveat:** There is **no** dedicated “raw LLM response” file; model output is reconstructed from **`assistant_delta` / `thinking_delta`** lines in `run.jsonl`.

---

## 2. Context ranking decisions (kept vs dropped)

**Directory:** `Saved/UnrealAiEditor/ContextDecisionLogs/<thread_id_sanitized>/`

Per build, the plugin may write:

- `<timestamp>.jsonl` — line-delimited JSON
- `<timestamp>-summary.md` — short human summary

**Jsonl shape:**

- `event: "meta"` — budget, token estimates, counts (`packedRetrievalL0/L1/L2Count`, `retrievalHeadSetSize`, kept/dropped counts, etc.).
- `event: "candidate"` — one row per candidate: `decision` (`kept` / `dropped`), `reason` / `dropReason`, `type`, `sourceId`, `entityId`, **`features`** (includes `vectorSimilarity` when used), **`scoreBreakdown`**.

| Question | Where to look |
|----------|----------------|
| Why was this chunk in or out of context? | Candidate row `decision`, `reason` / `dropReason`, `scoreBreakdown`. |
| How much retrieval by level made the pack? | `meta.packedRetrievalL0Count` etc. |
| Thread / project for this log? | `meta.threadId`, `meta.projectId` (and filename path). |

**Note:** These logs capture **selection metadata**, not necessarily the **full text** of every candidate body in each line. The **assembled string** that was emitted appears in context snapshots / `context_window_*.txt` / `llm_requests` messages.

Logging is controlled in code by `UnrealAiRuntimeDefaults::ContextDecisionLogEnabled` and/or verbose context build; if a directory is missing, builds may not have triggered logging for that path.

---

## 3. Manual context dump (editor console)

**Command:** `UnrealAi.DumpContextWindow <ThreadGuid> [reason_slug]`  
**Output:** `Saved/UnrealAiEditor/ContextSnapshots/<reason>_<UTC_ts>.txt`  
Optional: `<reason>_<UTC_ts>_trace.txt` when verbose context build is enabled for that dump.

| Question | Answer |
|----------|--------|
| What is the **exact** context block for a thread **right now** without running the LLM? | Read the `.txt` snapshot. |

**Command:** `UnrealAi.DumpContextDecisionLogs` — prints recent `ContextDecisionLogs` paths to the Output Log.

---

## 4. Retrieval / “vector” persistence

| Location | Content |
|----------|---------|
| `Saved/UnrealAiEditor/ContextUtility/<project_id>-retrieval-state.json` | **Aggregated** retrieval utility: per-entity counters, `headEntities`, edges — not full embedding tables. |
| `%LOCALAPPDATA%\UnrealAiEditor\vector\<project_id>\` (typical Windows) | SQLite-backed **vector index** for the project. If `LOCALAPPDATA` is empty, falls back under `Saved/UnrealAiEditor/vector/<project_id>/`. |

| Question | Approach |
|----------|----------|
| What did we **persist** about retrieval usefulness? | Open `-retrieval-state.json`. |
| Inspect **stored vectors / chunks** at DB level? | Use SQLite tooling on the DB under the `vector\<project_id>` path (implementation detail; schema in plugin retrieval code). |
| Which chunks **won ranking** for a given turn? | Prefer **`ContextDecisionLogs`** candidate rows + **`context_window_*.txt`** / **`llm_requests`** for final text. |

---

## 5. Toggles and defaults (when something is missing)

- **`llm_requests.jsonl`:** Written by the **harness file sink** only. **Editor Agent Chat UI** does **not** implement the same hook — expect **no** `llm_requests.jsonl` from normal UI-only runs unless code is extended.  
  Editor setting: **Harness → Log LLM requests to harness run folder** (`UUnrealAiEditorSettings::bLogLlmRequestsToHarnessRunFile`, default on for harness). Env override: `UNREAL_AI_LOG_LLM_REQUESTS` = `1` / `0` / `true` / `false`.
- **Per-tool context dumps:** Harness defaults and `RunAgentTurn` 5th arg `dumpcontext` / `nodump` (see `UnrealAiEditorModule` console help).
- **Verbose context traces in harness:** `UNREAL_AI_HARNESS_VERBOSE_CONTEXT=1` (and related defaults in `UnrealAiRuntimeDefaults.h`).
- **Dump context on run finished:** `UNREAL_AI_HARNESS_DUMP_CONTEXT_ON_FINISH=1` (optional; can block harness completion — see `FAgentRunFileSink`).
- **`console_command`:** Default allow-list only (`UnrealAiToolDispatch_Console.cpp`). **Legacy wide exec:** `UUnrealAiEditorSettings::bConsoleCommandLegacyWideExec` or env `UNREAL_AI_CONSOLE_COMMAND_LEGACY_EXEC` (`1` / `true` / `0` / `false`). See `docs/tooling/tool-dispatch-inventory.md`.

---

## 6. Other `Saved/UnrealAiEditor/` artifacts (secondary)

- **`Automation/tool_matrix_last.json`** — tool catalog matrix run output.
- **`MentionAssetIndex.json`** — mention index cache.
- **`ViewportCaptures/`** — viewport capture images when tools request them.
- **Plugin persistence** (threads/transcripts when using local stub) also lives under `Saved/UnrealAiEditor/` — see `FUnrealAiPersistenceStub` / persistence layer for **chat JSON** vs harness **run.jsonl**.

---

## 7. How to answer common user questions

| User asks | Do this |
|-----------|---------|
| “What did we send the model?” | **`llm_requests.jsonl`** (harness) or **`messages`** inside it; align with **`context_window_*.txt`**. For UI-only runs, use **`UnrealAi.DumpContextWindow`** or inspect persisted session if available. |
| “What did the model say?” | **`run.jsonl`**: concatenate `assistant_delta` / `thinking_delta` chunks in order for that run. |
| “Which tools ran and did they succeed?” | **`tool_start` / `tool_finish`** in **`run.jsonl`**. |
| “Why is file/scene/tree chunk X missing from context?” | **`ContextDecisionLogs`**: find candidate `sourceId` / `entityId`, read `decision`, **`dropReason`**, **`scoreBreakdown`**. |
| “What does the file tree / project blurb look like in context?” | **`context_window_*.txt`** or **`ContextSnapshots`**, or the **`user`/`system` message** in **`llm_requests.jsonl`**. |
| “What’s in the vector DB?” | On-disk **SQLite** under **`LOCALAPPDATA\...\vector\<project_id>`** (or project `Saved` fallback); for **ranking**, prefer decision logs + final prompts. |

---

## 8. Source pointers (for maintainers)

- Harness sink: `Plugins/UnrealAiEditor/.../Harness/FAgentRunFileSink.cpp`
- Context decisions: `Plugins/UnrealAiEditor/.../Context/UnrealAiContextDecisionLogger.cpp`
- Context build + trace: `Plugins/UnrealAiEditor/.../Context/FUnrealAiContextService.cpp`
- Console dump: `Plugins/UnrealAiEditor/.../UnrealAiEditorModule.cpp` (`UnrealAi.DumpContextWindow`)
- Defaults: `Plugins/UnrealAiEditor/.../Misc/UnrealAiRuntimeDefaults.h`, `UnrealAiEditorSettings.h`
- Tool router / aliases: `docs/tooling/tool-dispatch-inventory.md`, `UnrealAiToolDispatch.cpp`, `UnrealAiToolDispatch_Console.cpp`
