# Fine-tuning log

Running log of changes aimed at improving headed harness quality and API reliability. Entries are grouped into four areas: **context**, **tools**, **resolvers**, and **prompts**. Add new rows under each area inside a dated section when you land a meaningful change.

---

## How to add an entry

1. Start a new **date block** (UTC or local—be consistent; this file uses **local date** with ISO time where useful).
2. Under each affected area, use bullet points. Use `—` for scope; keep it factual (what changed, where in the tree).
3. If an area had **no** changes that cycle, write `— (none)` under that heading.

---

## 2026-03-28 — timeouts: liberal hardcoded HTTP + harness sync (no env for chat)

### Harness / reliability

- **`UnrealAiRuntimeDefaults.h`:** **`HttpRequestTimeoutSec` 1200s**; **`HarnessSyncWaitMs` 1500000ms** (~25 min/segment); **`StreamToolIncompleteMaxMs` 180000**; source-only (not `.env`).
- **`FOpenAiCompatibleHttpTransport.cpp`:** Chat timeout uses **`HttpRequestTimeoutSec`** only (429 retry count still optional env).
- **`UnrealAiHarnessScenarioRunner.cpp`:** Sync wait uses **`HarnessSyncWaitMs`** only.
- **`run-long-running-headed.ps1`:** No timeout env/parameters; editor focus **off** by default (**`-BringEditorToForeground`** opt-in).

---

## 2026-03-28 — harness: stream tool timeout — placeholder slots + lenient caps

### Harness / reliability

- **`FUnrealAiAgentHarness.cpp`:** Streamed `tool_calls` deltas can use **`index` > 0** on the first chunk; **`MergeToolCallDeltas`** pads with defaulted slots at **0..index-1**. Those rows were “incomplete” forever and burned the **event** budget with **`age_ms=0`**. Now **placeholder** slots (empty id, name, args) are **ignored** for first-seen + incomplete timeout. First-seen maps are keyed **only** by **pending array index** `I` so padding cannot alias with real tools.
- **`UnrealAiRuntimeDefaults.h`:** **`StreamToolIncompleteMaxEvents`** **12 → 96**, **`StreamToolIncompleteMaxMs`** **2500 → 90000** so slow/chunky provider streams do not false-fail mid-JSON.

---

## 2026-03-28 — harness: streamed tool-call timeout map key + headed script metric label

### Context

- **`run-long-running-headed.ps1`:** Batch banner **`tool_calls`** was misleading—it summed **`tool_finish` JSONL events**, not model tool-call chunks. Renamed display to **`tool_finish_events`** (kept **`tool_calls`** in the brief object as the same count for any legacy readers). Clarifies empty counts when turns fail before any tool completes.

### Tools

- — (none)

### Resolvers

- — (none)

### Prompts

- — (none)

### Harness / reliability

- **`FUnrealAiAgentHarness.cpp`:** Incomplete streamed `unreal_ai_dispatch` tracking stored first-seen telemetry under **`StreamMergeIndex`** but `CheckIncompleteToolCallTimeout` read **`PendingToolCalls` index `I`**, so **`age_ms` stayed 0** and **`age_events`** was measured from the wrong baseline. **`MergeToolCallDeltas`** now assigns **`Slot.StreamMergeIndex`**. After each ToolCalls merge, first-seen maps are seeded for **every** incomplete pending slot using **`MapKey = StreamMergeIndex >= 0 ? StreamMergeIndex : I`**, and timeout uses the same key—restores wall-clock + per-tool event budgets.

---

## 2026-03-28 — viewport framing: reject misused level tokens; clearer errors

### Context

- — (none)

### Tools

- **`UnrealAiToolDispatch_Viewport.cpp` — `viewport_frame_actors`:** Before resolving actors, reject common bad `actor_paths` entries (`PersistentLevel` and `WorldSettings` as the whole string, plus empty strings) with an explicit explanation and **`suggested_correct_call`** to **`scene_fuzzy_search`** (example query `Actor`). When no bounds could be built after resolution, the error message now states that the level instance name must not be used alone.
- **`UnrealAiToolDispatch_Viewport.cpp` — `viewport_frame_selection`:** Error text when selection cannot be framed now points to discovery + framing, and to **`viewport_camera_get_transform` / dolly / orbit** for camera-only tasks (reduces conflating “no selection” with “cannot move camera”).
- **`UnrealAiToolCatalog.json` — `viewport_frame_actors` / `viewport_set_view_mode`:** Summary, `actor_paths` description, and failure_modes updated so the catalog no longer reads as if “containing PersistentLevel” could mean passing `PersistentLevel` alone; view mode is documented as independent of framing.

### Resolvers

- — (none)

### Prompts

- **`prompts/chunks/04-tool-calling-contract.md`:** `viewport_frame_actors` example path corrected to a full world shape and explicit “never `PersistentLevel` alone” note.

---

## 2026-03-28 — plan-mode harness: per-segment sync wait; catalog `fast` mode removed

### Context

- **`UnrealAiHarnessScenarioRunner.cpp`:** For **`EUnrealAiAgentMode::Plan`**, the game-thread sync loop now waits on **completion** or on **plan sub-turn** signals with a **fresh** **`UnrealAiRuntimeDefaults::HarnessSyncWaitMs`** window each segment (planner `RunTurn` then each serial node `RunTurn`), instead of one deadline for the entire DAG. Uses manual-reset `PlanSubTurnEvent` + `WaitForDoneOrPlanSubTurnWhilePumpingGameThread` (prefers `Done` when both fire). Pool return on success and timeout paths.
- **`IAgentRunSink.h` / `FAgentRunFileSink`:** New hook **`OnPlanHarnessSubTurnComplete()`** (optional file-sink event trigger); plan executor calls it after a successful planner parse (non–pause-for-build path) and at the start of each **`OnNodeFinished`** so automation can reset the wait budget.
- **`FUnrealAiPlanExecutor.cpp`:** Emits **`OnPlanHarnessSubTurnComplete`** after **`OnRunContinuation`** post-planner; **not** emitted when **`bPauseAfterPlannerForBuild`** returns early (editor review path unchanged). **`tests/long-running-tests/run-long-running-headed.ps1`:** Comment + batch banner text updated to describe **per-segment** Plan sync vs single end-to-end deadline.

### Tools

- **`UnrealAiToolCatalog.json`:** Removed redundant **`modes.fast`** entries across tools (Agent roster used **`agent`** only; **`fast` was duplicate truth**).
- **`UnrealAiToolCatalog.cpp` — `PassesModeAndPack`:** **`EUnrealAiAgentMode::Agent`** no longer falls back to **`modes.fast`** when **`agent`** is false—reduces accidental inclusion from a legacy flag. **Plan** mode still falls back to **`agent`** when **`plan`** is false.

### Resolvers

- — (none)

### Prompts

- — (none)

---

## 2026-03-28 — pre-iteration: harness sync default, localized catalog nudges (no global prompt bloat)

### Context

- **`tests/long-running-tests/run-long-running-headed.ps1` (historical):** Raised per-run sync budget for plan steps (later **superseded**: **`HarnessSyncWaitMs`** is hardcoded in **`UnrealAiRuntimeDefaults.h`**; script no longer sets timeout env/args).

### Tools

- **`UnrealAiToolCatalog.json` — `asset_index_fuzzy_search`:** Summary + **`query`** description nudge: on vague domain-scoped user asks, **prefer one bounded fuzzy/registry call** (topical query + narrow `path_prefix`) **over** asking the user for a keyword first; align with headed run **fine-tune-01 step_07** where the model replied conversationally instead of searching. *Why:* steer via **per-tool** catalog text (paid when the tool is in the roster), not a long new paragraph in **`04-tool-calling-contract.md`** (global token cost).
- **Same file — `material_get_usage_summary`:** Summary line + **`failure_modes`** — if not found, retry discovery with **`asset_index_fuzzy_search`** before concluding absence.

### Resolvers

- — (none)

### Prompts

- — (none — intentional; vague-discovery steering is catalog-local this cycle.)

### Iteration stance (for later critique)

- **100% harness completion is not the goal**; we still expect residual **`tool_finish_false`**, qualitative misses, and API variance.
- **Harness hygiene** (sync wait vs plan executor cost) is owned by **script/C++ bounds**; **LLM policy** nudges for “call search before chit-chat” are **localized catalog** lines to avoid growing always-on prompt chunks.
- **Coarse metrics:** `harness-classification.json`; **precise:** `run.jsonl` tool lines. We may revise this split if metrics mislead.

---

## 2026-03-28 — empty arguments, schema guidance, catalog semantics

### Approach, metrics, and epistemic limits (meta)

**What we were trying to do:** Improve real editor sessions where the model calls tools with **empty `{}`** or missing required fields, then **retries the same invalid shape** until the harness stops the turn. We treated that as primarily a **contract problem** (prompt text contradicted the JSON schema), secondarily **documentation** (catalog promised “folder” behavior the implementation did not provide), and thirdly **resolver UX** (errors should **route** to discovery tools via `suggested_correct_call`, not only scold).

**How we tested / “fine-tuned” in practice:**

- **Headed long-running harness** (`tests/long-running-tests/run-long-running-headed.ps1`) with **`MaxSuites`** to limit cost; outputs under `tests/long-running-tests/runs/run_*`.
- **`harness-classification.json`** (from `tests/classify_harness_run_jsonl.py`): buckets such as `tool_finish_false`, `harness_policy`, `http_timeout`, `rate_limit` — useful for **coarse** regressions, **not** a precision metric for tool correctness.
- **Qualitative drill-down:** read **`run.jsonl`** per turn for `tool_start` / `tool_finish` lines — **only** way to see **exact** `arguments_json` and whether failures were **validation** vs **semantic** (e.g. empty `{}` vs wrong path).

**Metrics we used (and how they can mislead):**

- **`tool_finish_false` counts:** Count **events**, not “accuracy.” More failures can mean **more LLM rounds**, **more retries**, or **stricter logging** — not necessarily worse handlers.
- **Per-tool failure rates** across batches: confounded by **different suites completed** (e.g. old run truncated a suite), **sampling variance**, and **429/timeouts** aborting turns.
- **`harness_policy`:** Measures **our** harness rules, not user-visible tool quality.

**Thought process to preserve (so we can critique it later):** We assumed (1) **prompt fixes scale** to many user inputs better than one-off C++ branches, (2) **catalog truth** should match implementation to avoid training the model on lies, (3) **suggested_correct_call toward discovery** beats repeating the same tool with a fake placeholder path. **Possible future verdict:** Prompts may be **too verbose** for token budget; discovery-first `suggested_correct_call` might **steer away** from legitimate sync-after-path flows; we **did not** A/B test with controlled model seeds.

### Context

- — (none — no new env toggles or context service logic this iteration.)

### Tools

- **`UnrealAiToolCatalog.json` — `content_browser_sync_asset`:** Summary and `path` description now state that sync targets a **resolvable asset object path** via the registry; folder-only strings are not a substitute for navigation; **failure_modes** for missing path vs non-resolving path. *Why:* implementation uses `GetAssetByObjectPath`; prior “asset or folder” copy did not match behavior.
- **Same file — `viewport_frame_actors` / `viewport_frame_selection`:** Clarified that **`actor_paths` is required** for the actors variant; pointed to **`editor_get_selection` / `scene_fuzzy_search`** when paths are unknown; **`viewport_frame_selection`** described as the path-free option when selection is already set. *Why:* reduce `{}` calls and wrong-tool confusion for framing.

### Resolvers

- **`UnrealAiToolDispatch_Context.cpp` — `UnrealAiDispatch_ContentBrowserSyncAsset`:** When `path` is missing/empty, error text now states that **empty arguments are invalid** and paths must come from discovery or context; **`suggested_correct_call`** now targets **`asset_index_fuzzy_search`** (generic `query` + `path_prefix`) instead of re-suggesting sync with a placeholder asset path. *Why:* route the model to **find** a real path before sync; breaks “identical empty sync” retry loops in harness telemetry.
- **`UnrealAiToolDispatch_Viewport.cpp` — `UnrealAiDispatch_ViewportFrameActors`:** Missing/empty `actor_paths` now uses **`ErrorWithSuggestedCall`** toward **`editor_get_selection`**, with message text referencing **`scene_fuzzy_search`** and **`viewport_frame_selection`**. *Why:* teach recovery without hardcoding test scenarios.

### Prompts

- **`prompts/chunks/04-tool-calling-contract.md`:** Removed guidance that encouraged **`{}` when arguments are unknown** for **named** tools; reserved `{}` for schemas with **no required fields**; added **“Required arguments (schema-first)”** subsection; added minimal examples for **`content_browser_sync_asset`** and **`viewport_frame_actors`**; clarified that **empty `{}` with required keys** is the same invalid retry.
- **`prompts/chunks/05-context-and-editor.md`:** Stated that **selection, framing, and Content Browser sync** need **concrete paths** from context or discovery.

---

## 2026-03-28 — asset search perf, registry guard, dispatch suggestions, HTTP 400 diagnostics

### What we fixed (concrete)

- **`asset_index_fuzzy_search` no longer allocates the entire `/Game` asset list.** Implementation now uses **`IAssetRegistry::EnumerateAssets`** and stops after **`max_assets_to_scan`** visits (default **12000**), with **`truncated`**, **`assets_visited`**, and sorted top matches unchanged in spirit. This removes the dominant **multi-minute game-thread stall** caused by **`GetAssets` filling a huge `TArray` before any fuzzy scoring.
- **Empty or missing `query` without an explicit `path_prefix` is rejected** with **`ErrorWithSuggestedCall`** (example `query` + narrow `path_prefix`). Previously, **`{}`** coerced **`query`** to **`PlayerStart`** and still scanned **all assets under the default prefix**—expensive and easy to trigger from bad tool args.
- **`asset_registry_query` cannot run with a completely empty filter** (no `path_filter` and no `class_name` after aliases). That case previously risked an **unbounded registry enumeration** depending on engine behavior.
- **Blueprint and material “path required” tools** now return **`ErrorWithSuggestedCall`** with example paths and/or a discovery tool id (**`blueprint_compile`**, **`blueprint_get_graph_summary`**, **`material_get_usage_summary`**, plus clearer copy on **`asset_open_editor`**). **`asset_save_packages`** already suggested paths when empty; **`editor_set_selection`** already had **`ErrorWithSuggestedCall`**.
- **HTTP 400 from the chat API** now triggers **structured logging** of whether our **outbound request body parses as JSON** and head/tail snippets, so **`invalid_request_error` / “could not parse the JSON body”** can be tied to a malformed **request** build rather than guessed.

### Earlier iterations: what we tried, why it was insufficient, why this pass is different

**Earlier same-day / prior work (still valuable, but not sufficient alone):**

| Area | What we did | Why it did not fully solve the failures we saw in `run.jsonl` |
|------|-------------|--------------------------------------------------------------|
| **Prompts** (`04-tool-calling-contract.md`, `05-context-and-editor.md`) | Told the model not to use **`{}`** when schemas have **required** fields; schema-first examples. | LLMs still emit **`{}`** under load or when copying the wrong pattern; **policy text does not cap CPU** or block invalid API shapes at the dispatcher. |
| **Catalog** | Truthy descriptions for **`content_browser_sync_asset`**, viewport frame tools; **`failure_modes`**. | Reduces **training/documentation drift** but does not stop **repeated identical tool failures** if the model ignores the text. |
| **Resolvers** | **`content_browser_sync_asset`** → **`suggested_correct_call`** to **`asset_index_fuzzy_search`**; **`viewport_frame_actors`** → discovery suggestions. | Correct **routing intent**, but **`asset_index_fuzzy_search`** was itself implemented as **full `GetAssets` under `/Game`**—so routing **more** discovery traffic to it could **worsen** freezes when the model called fuzzy search with **`{}`** (coerced query + full tree). |
| **Harness** (“lax policy”) | Fewer synthetic nudges; telemetry-only mutation notes; no hard fail on text-only completions. | Improves **orchestration noise** and false “harness_policy” pressure; does **not** fix **tool validation**, **registry cost**, or **HTTP 400** request bodies. |
| **HTTP transport** | Smarter **429** / **`Retry-After`**. | Addresses **rate limits**, not **400 bad JSON** or **local** request serialization bugs. |

**Why the fixes in *this* iteration are expected to work:**

1. **Algorithm + bounds (necessary):** **`EnumerateAssets` + visit cap** address **real CPU and allocation cost** regardless of model behavior. Prompts cannot shrink **`GetAssets`**’ output array; only different APIs or stricter filters can.
2. **Explicit gate on the worst `{}` path (necessary):** Requiring **`path_prefix`** when **`query` is coerced** stops the **default “scan everything under `/Game`”** behavior that matched **minute-long** stalls in headed runs.
3. **Registry query guard (necessary):** Rejecting **unbounded** filters prevents another **full-catalog** footgun parallel to fuzzy search.
4. **Dispatcher-level `ErrorWithSuggestedCall` (complements prompts):** Gives the model a **machine-readable next step** for **`blueprint_*`**, **`material_get_usage_summary`**, etc., not only prose—reduces **identical `tool_id\|{}` signatures** that trigger harness stops.
5. **HTTP 400 logging (diagnostic):** Separates **our JSON bug** (non-parsing outbound body) from **provider quirks**; earlier iterations had no visibility there.

**Caveat:** **`asset_index_fuzzy_search` with an explicit `query` and default `path_prefix` `/Game`** can still be heavy on huge projects until the visit cap stops enumeration; the cap trades **completeness** for **responsiveness**. **`asset_registry_query`** with only a **class** filter can still be large—bounded compared to “everything,” but not “free.”

### Context

- — (none)

### Tools

- **`UnrealAiToolCatalog.json`:** `asset_index_fuzzy_search` — summary/parameters aligned with **EnumerateAssets** + visit cap; **`assets_visited`**, **`truncated`**, **`failure_modes`**. `asset_registry_query` — bounded-query requirement in summary + failure mode.
- **`UnrealAiToolDispatch_Search.cpp` — `asset_index_fuzzy_search`:** Uses **`IAssetRegistry::EnumerateAssets`** with **`max_assets_to_scan`** as a hard visit cap; rejects **missing/empty query** unless **`path_prefix`** is explicitly set (narrow search). Default **`max_assets_to_scan`** lowered to **12000**.

### Resolvers

- **`UnrealAiToolDispatch_Context.cpp` — `asset_registry_query`:** Returns **`ErrorWithSuggestedCall`** when **both** `path_filter` and `class_name` are absent after all alias parsing (avoids unbounded **`GetAssets`**).
- **`UnrealAiToolDispatch_BlueprintTools.cpp`:** **`blueprint_compile`** / **`blueprint_get_graph_summary`** — **`ErrorWithSuggestedCall`** when **`blueprint_path`** missing.
- **`UnrealAiToolDispatch_AssetsMaterials.cpp` — `material_get_usage_summary`:** **`ErrorWithSuggestedCall`** → **`asset_index_fuzzy_search`** when **`material_path`** missing.
- **`UnrealAiToolDispatch_MoreAssets.cpp` — `asset_open_editor`:** Clearer error text (discovery-first).

### Prompts

- **`prompts/chunks/04-tool-calling-contract.md`:** Notes on **`asset_index_fuzzy_search`** (**`path_prefix`** when query omitted) and bounded **`asset_registry_query`** (aligned with handlers).

### Transport

- **`FOpenAiCompatibleHttpTransport.cpp`:** On **HTTP 400**, logs **outbound request JSON parse_ok**, length, head/tail, and response prefix for **invalid JSON body** debugging.

---

## 2026-03-28 — harness lax policy, runner, HTTP transport (earlier same day)

### Context

- **Harness enforcement (lax policy).** `FUnrealAiAgentHarness.cpp`: removed hard failures and synthetic user nudges that required tool calls on every “action-intent” assistant turn when the model replied with text only (including after successful tools). Removed the generic post-tool `[Harness][reason=tool_round_complete]` nudge that pushed another tool round. Softened `todo_plan_only` copy. Mutation “read-only only” paths no longer inject a forcing user message; they emit `mutation_read_only_note` for telemetry only. Text-only completions now emit `action_text_only_completion` instead of failing. *Rationale:* orchestration + `run.jsonl` / tool metrics for unknown tools and bad schema; task success is judged qualitatively.
- **Long-running headed runner.** `tests/long-running-tests/run-long-running-headed.ps1`: capture Unreal `Saved/Logs` project log per editor session as `editor_console_saved.log` (and stdio redirects where applicable); in single-session mode, copy full batch log to each suite as `editor_console_full_batch.log`. Added `-MaxSuites` (`0` = all; `N` or `-N` = first `|N|` suites by sorted path) to cut API usage during iteration.

### Tools

- — (none this cycle — tool dispatch / catalog unchanged in this pass; harness still records `tool_finish` success/failure for quantitative review.)

### Resolvers

- **HTTP 429 / rate limits.** `FOpenAiCompatibleHttpTransport.cpp`: retries now **wait as long as the provider indicates** when a hint exists: `Retry-After` as seconds or HTTP-date (`FDateTime::ParseHttpDate`), optional `error.retry_after`, and “try again in …” text in JSON `error.message`. Authoritative waits are no longer capped at 120s or inflated with a minimum exponential backoff; exponential backoff applies only when **no** usable hint is present. Reduces spurious failures when TPM windows exceed two minutes.

### Prompts

- — (none this cycle — no updates to `prompts/chunks` or prompt builder content in this pass.)

---

## Long-running harness coverage vs tool catalog (audit)

**Purpose:** The headed fine-tune suites use **vague, user-like prompts** on purpose. This section maps those tasks to [`Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`](../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json) (`tool_count` **81**) so we can see **overfitting risk** (repeatedly exercising the same small subset) and **gaps** (editor surfaces never stressed by harness turns). *Qualitative success is still judged outside raw tool counts; this is a coverage checklist for future scenarios.*

### Suites included in this audit

| Suite | Focus |
|-------|--------|
| [`tests/long-running-tests/fine-tune-01-tool-definitions/suite.json`](../tests/long-running-tests/fine-tune-01-tool-definitions/suite.json) | Scene vs content search, open asset, blueprint read/compile, fuzzy **asset** search, material usage + scalar MI tweak, PIE start/status/stop, save packages |
| [`tests/long-running-tests/fine-tune-02-resolver-harness/suite.json`](../tests/long-running-tests/fine-tune-02-resolver-harness/suite.json) | Selection, open/sync/frame, **plan** + **agent** modes, snapshot read, PIE, save, continuation |
| [`tests/long-running-tests/fine-tune-03-prompts-contract/suite.json`](../tests/long-running-tests/fine-tune-03-prompts-contract/suite.json) | Action-intent / no empty narration; compile, format graph, save, PIE, message log, actor transform, MI scalar, asset create |
| [`tests/long-running-tests/fine-tune-04-context-management/suite.json`](../tests/long-running-tests/fine-tune-04-context-management/suite.json) | Long-horizon recall; snapshot, actor list/transform, scene + asset queries, viewport frame, PIE cycle, message log, compile, save |
| `domain_scenario_refs` in fine-tune-03 | [`tests/vague_*.json`](../tests/) — thematic flavor only (same broad BP / level / material / PIE cluster); **not** separate automated turns unless wired elsewhere |

### Tools that the current vague tasks *reliably* exercise (high repetition)

These families show up across multiple turns or suites: **discovery** (`scene_fuzzy_search`, `asset_index_fuzzy_search`, `asset_registry_query`, `actor_find_by_label`), **editor selection / framing** (`editor_get_selection`, `editor_set_selection`, `content_browser_sync_asset`, `viewport_frame_selection` / `viewport_frame_actors`), **blueprint read/compile/format** (`blueprint_get_graph_summary`, `blueprint_compile`, `blueprint_format_graph`), **materials** (`material_get_usage_summary`, `material_instance_set_scalar_parameter`), **PIE** (`pie_start`, `pie_status`, `pie_stop`), **persistence** (`asset_save_packages`), **read-only state** (`editor_state_snapshot_read`, `engine_message_log_read`), **world tweak** (`actor_get_transform`, `actor_set_transform`), **orchestration** (`agent_emit_todo_plan` in plan turns), and sometimes **`asset_open_editor`**, **`asset_create`**.  
**Risk:** The model can look strong on “find BP → compile → PIE → save → nudge MI” while other catalog tools stay cold.

### Catalog tools with **little or no** dedicated harness pressure (gaps)

Grouped by catalog `category` / theme. *Unless noted, assume “not targeted by a turn that asks for that capability by name.”*

**World / actors**

- `actor_attach_to`, `actor_destroy`, `actor_set_visibility`, `actor_blueprint_toggle_visibility`, `actor_spawn_from_class` — suites avoid destructive spawn/delete; attachment/visibility/spawn are not asked explicitly.

**Animation / Sequencer**

- `animation_blueprint_get_graph_summary`, `sequencer_open`, `level_sequence_create_asset` — no “open Level Sequence” or AnimBP graph tasks.

**Audio / MetaSounds**

- `audio_component_preview`, `metasound_open_editor` — no audio graph or preview prompts.

**Assets (mutations beyond save/create)**

- `asset_delete`, `asset_duplicate`, `asset_rename`, `asset_import`, `asset_get_metadata` (as primary task), `asset_export_properties`, `asset_apply_properties`, `asset_find_referencers`, `asset_get_dependencies` — turns rarely ask for dependency/referencer graphs or bulk metadata/property round-trips.

**Blueprint (advanced)**

- `blueprint_add_variable`, `blueprint_apply_ir`, `blueprint_open_graph_tab`, `blueprint_export_ir` — harness prefers **format_graph** over **apply_ir**; no “add variable” / export IR / open specific graph tab” vague tasks.

**Build / cook / package**

- `cook_content_for_platform`, `package_project`, `shader_compile_wait` — not exercised (vague build workflow JSON exists but is **not** the same as driving these tools from `suite.json` turns).

**Collision / physics**

- `collision_trace_editor_world`, `physics_impulse_actor` — no trace/impulse prompts.

**Console / arbitrary execution**

- `console_command`, `raw_user_exec_string` — suites often *discourage* “random console” in favor of dedicated tools; these paths stay cold (intentionally for safety).

**Dangerous / sandbox**

- `arbitrary_network_fetch`, `arbitrary_process_spawn`, `arbitrary_python_eval`, `delete_system_files` — should **remain** rare or absent in default harness (policy).

**Editor UI / navigation**

- `content_browser_navigate_folder`, `global_tab_focus`, `menu_command_invoke`, `editor_set_mode` — limited; we use **sync** more than folder navigation or menu command IDs.

**Landscape / foliage / PCG**

- `landscape_import_heightmap`, `foliage_paint_instances`, `pcg_generate` — no environment-art tasks.

**Materials (secondary params)**

- `material_instance_set_vector_parameter` — only **scalar** MI tweaks are requested; vector/color MI paths untested.

**Networking / subagents**

- `subagent_spawn`, `worker_merge_results` — no multi-agent harness turns in these suites.

**Project / source**

- `project_file_read_text`, `project_file_write_text`, `source_search_symbol` — no “edit C++” or `.uproject` text vague loops.

**Rendering / capture**

- `viewport_capture_png`, `viewport_capture_delayed`, `viewport_set_view_mode`, `render_target_readback_editor` — no screenshot / view-mode / RT readback tasks.

**Viewport camera (direct manipulation)**

- `viewport_camera_dolly`, `viewport_camera_orbit`, `viewport_camera_pan`, `viewport_camera_pilot`, `viewport_camera_get_transform`, `viewport_camera_set_transform` — suites use **frame** tools, not free camera drives.

**Misc**

- `outliner_folder_move`, `tool_audit_append` — not targeted as primary user goals.

### Summary

- **We are not “fine-tuning on 81 tools.”** The four suites repeatedly stress a **~20-tool corridor** (discovery + BP + MI scalar + PIE + save + snapshot/log + light actor transform). That is **broader than a single scripted macro** but **narrower than the full editor surface** implied by the catalog.
- **Overfitting risk** is moderate: tasks are vague, but **tool choice diversity** within a session is still skewed toward the same orchestration patterns.
- **Next steps (backlog):** add additional vague suites or turns that rotate through **Sequencer / audio / landscape-PCG / packaging / viewport camera / vector MI / referencers / C++ symbol search / capture** without abandoning the “natural user” tone—track implementation under **Tools** in a future dated block above.

### Per-tool coverage key (all 81 `tool_id` values)

Legend: **high** = repeatedly invited by current vague turns; **low** = may appear opportunistically or once; **gap** = not meaningfully targeted; **policy** = intentionally absent from default harness (dangerous / arbitrary execution).

| `tool_id` | Coverage |
|-----------|----------|
| `actor_attach_to` | gap |
| `actor_destroy` | gap |
| `actor_find_by_label` | high |
| `actor_get_transform` | high |
| `actor_set_transform` | high |
| `actor_set_visibility` | gap |
| `actor_blueprint_toggle_visibility` | gap |
| `actor_spawn_from_class` | gap |
| `animation_blueprint_get_graph_summary` | gap |
| `arbitrary_network_fetch` | policy |
| `arbitrary_process_spawn` | policy |
| `arbitrary_python_eval` | policy |
| `asset_delete` | gap |
| `asset_duplicate` | gap |
| `asset_get_metadata` | gap |
| `asset_import` | gap |
| `asset_open_editor` | high |
| `asset_registry_query` | high |
| `asset_rename` | gap |
| `asset_save_packages` | high |
| `audio_component_preview` | gap |
| `blueprint_add_variable` | gap |
| `blueprint_compile` | high |
| `blueprint_apply_ir` | low |
| `blueprint_format_graph` | high |
| `blueprint_get_graph_summary` | high |
| `blueprint_open_graph_tab` | gap |
| `collision_trace_editor_world` | gap |
| `console_command` | gap |
| `content_browser_navigate_folder` | gap |
| `content_browser_sync_asset` | high |
| `cook_content_for_platform` | gap |
| `delete_system_files` | policy |
| `editor_get_selection` | high |
| `editor_set_mode` | gap |
| `editor_set_selection` | high |
| `editor_state_snapshot_read` | high |
| `engine_message_log_read` | high |
| `foliage_paint_instances` | gap |
| `global_tab_focus` | gap |
| `landscape_import_heightmap` | gap |
| `material_get_usage_summary` | high |
| `material_instance_set_scalar_parameter` | high |
| `material_instance_set_vector_parameter` | gap |
| `menu_command_invoke` | gap |
| `metasound_open_editor` | gap |
| `outliner_folder_move` | gap |
| `package_project` | gap |
| `pcg_generate` | gap |
| `physics_impulse_actor` | gap |
| `pie_start` | high |
| `pie_status` | high |
| `pie_stop` | high |
| `project_file_read_text` | gap |
| `project_file_write_text` | gap |
| `raw_user_exec_string` | gap |
| `render_target_readback_editor` | gap |
| `sequencer_open` | gap |
| `shader_compile_wait` | gap |
| `source_search_symbol` | gap |
| `scene_fuzzy_search` | high |
| `asset_index_fuzzy_search` | high |
| `subagent_spawn` | gap |
| `tool_audit_append` | gap |
| `viewport_camera_dolly` | gap |
| `viewport_camera_get_transform` | gap |
| `viewport_camera_orbit` | gap |
| `viewport_camera_pan` | gap |
| `viewport_camera_pilot` | gap |
| `viewport_camera_set_transform` | gap |
| `viewport_capture_delayed` | gap |
| `viewport_capture_png` | gap |
| `viewport_frame_actors` | high |
| `viewport_frame_selection` | high |
| `viewport_set_view_mode` | gap |
| `agent_emit_todo_plan` | high |
| `asset_create` | low |
| `asset_export_properties` | gap |
| `asset_apply_properties` | gap |
| `asset_find_referencers` | gap |
| `asset_get_dependencies` | gap |
| `level_sequence_create_asset` | gap |
| `blueprint_export_ir` | gap |
| `worker_merge_results` | gap |

Roughly **24** tools are **high**-traffic in practice for these suites, **~2** **low**, **~51** **gap**, and **4** **policy** (arbitrary/destructive OS-level tools as cataloged).

---

<!-- New dates go above this line (newest first). -->
