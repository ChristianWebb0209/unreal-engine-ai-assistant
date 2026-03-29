# Unreal AI Editor plugin — agent handoff context

## Repo & build
- **UE 5.7** syntax; plugin: `Plugins/UnrealAiEditor/`.
- Build: `.\build-editor.ps1 -Headless` (use `-Restart` if plugin DLL is locked).
- **“Testing”** in this repo (unless user says otherwise) usually means **headed harness**: `tests/long-running-tests/run-long-running-headed.ps1`, suites under `tests/long-running-tests/`, outputs in `tests/long-running-tests/runs/run_*`. See `tests/long-running-tests/last-suite-summary.json` for latest batch pointer.

## Central runtime defaults (single source of truth)
- **`Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Misc/UnrealAiRuntimeDefaults.h`**
- **Chat HTTP timeout** (`HttpRequestTimeoutSec`): **hardcoded** (currently **1200s**). **Not** read from `.env`.
- **Harness game-thread sync** (`HarnessSyncWaitMs`): **hardcoded** (currently **~25 min/segment**). **Not** env-driven.
- **Embedding HTTP timeout** (`EmbeddingHttpTimeoutSec`): **hardcoded** in the same header (fail-fast for embedding calls).
- **Streamed incomplete tool-call guard**: `StreamToolIncompleteMaxEvents` / `StreamToolIncompleteMaxMs` (tool JSON split across SSE chunks).

## Reliability fixes already landed (don’t re-debug blindly)
1. **HTTP 400 — `messages.[n].role` tool without prior `tool_calls`**  
   - **Cause:** `UnrealAiTurnLlmRequestBuilder` trimmed context with naive `RemoveAt(1)`, dropping an assistant row while leaving following **`tool`** rows.  
   - **Fix:** `TrimApiMessagesForContextBudget` removes assistant **with** contiguous `tool` rows; strips leading orphan `tool` rows. **`UnrealAiConversationJson::MessagesToChatCompletionsJsonArray`** strips `tool` rows immediately after `system` as a safety net.

2. **False `stream_tool_call_incomplete_timeout` (often `age_ms=0`)**  
   - **Cause:** Stream deltas with `index>0` on first chunk created placeholder slots; wrong merge-index vs pending index.  
   - **Fix:** `FUnrealAiAgentHarness` / `MergeToolCallDeltas` — ignore placeholder slots for first-seen + timeout; align **`StreamMergeIndex`** with timeout keying; raised event/time caps in `UnrealAiRuntimeDefaults.h`.

3. **Wall-clock timeouts on normal prompts**  
   - Old **30s** HTTP cap + short harness sync caused spurious failures. Now **liberal hardcoded** HTTP + sync (see defaults header). **429** retries use `Http429MaxAttempts` in the same header.

4. **Viewport**  
   - `viewport_frame_actors` rejects **`PersistentLevel` / `WorldSettings` alone** + clearer errors; catalog + `prompts/chunks/04-tool-calling-contract.md` updated.

## Headed harness script (`tests/long-running-tests/run-long-running-headed.ps1`)
- **No** `-HttpTimeoutSec` / `-SyncWaitMs` — limits come from C++ only.
- **Editor focus:** default **does not** bring Unreal to foreground; **`-BringEditorToForeground`** opt-in.
- Still uses `Import-RepoDotenv` for **`UE_ENGINE_ROOT`** and **`OPENAI_*`** (see `.env.example`); provider JSON sync via `scripts/sync-unreal-ai-from-dotenv.ps1`.

## Key code paths
| Area | Files |
|------|--------|
| LLM HTTP | `FOpenAiCompatibleHttpTransport.cpp` |
| Message JSON | `UnrealAiConversationJson.cpp` |
| Turn build + context trim | `UnrealAiTurnLlmRequestBuilder.cpp` |
| Harness / stream tools | `FUnrealAiAgentHarness.cpp` |
| Scenario sync wait | `UnrealAiHarnessScenarioRunner.cpp` |
| Plan DAG | `Plugins/UnrealAiEditor/.../Private/Planning/FUnrealAiPlanExecutor.cpp` |
| Tool dispatch / viewport | `UnrealAiToolDispatch_Viewport.cpp` |
| Catalog | `Resources/UnrealAiToolCatalog.json` |

## Operational changelog
- **`tests/long-running-tests/tool-iteration-log.md`** — numbered entries (`Entry 1` … `Entry N`, newest first) for harness/tool/prompt changes; prepend **`## Entry M — …`** when logging (see file header).
- **`docs/api/timeout-handling.md`** — inventory of HTTP / harness / tool timeouts and sync behavior.

## Before a new harness batch
1. Rebuild plugin; **restart** editor so DLL matches.  
2. Don’t rely on `.env` for chat/sync timeouts (ignored for those).  
3. Parse failures: **HTTP transport** vs **`sync_wait_timeout` in log** vs **tool/policy** vs **400 body** in `run.jsonl` `error_message`.

## Intentional constraints (from user)
- Prefer **minimal, focused** diffs; match existing style.  
- Don’t expand markdown/docs unless asked (exception: operational logs like `tests/long-running-tests/tool-iteration-log.md` when recording changes).
