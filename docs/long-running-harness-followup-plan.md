# Long-running harness: analysis scope and follow-up (unambiguous)

This document replaces informal “plan” notes. It separates **(A) a frozen historical batch** from **(B) the current repo behavior**, so we do not re-fix work that is already merged or chase metrics from the wrong artifact.

## A. Which run is “the one we analyzed”?

### Frozen reference batch (fine-tune analysis)

- **Batch id:** `20260328-184159` (this string is the old **`batch_stamp`**: UTC wall-clock encoded as `yyyyMMdd-HHmmss`, **not** local time).
- **Produced by:** Older `run-long-running-headed.ps1` behavior: deleted each `runs\<path-slug>\` before writing, then `runs\<path-slug>\run_20260328-184159\`.
- **Aggregate file:** [`tests/long-running-tests/last-suite-summary.json`](../tests/long-running-tests/last-suite-summary.json) — when it still lists `batch_stamp` = `20260328-184159` and `run_root` paths like `...\runs\fine-tune-01-tool-definitions__suite\run_20260328-184159`, you are looking at **this** batch.
- **What counts as “pass” for that runner:** `failed_suite_count == 0` and every expected `run.jsonl` contains a `run_finished` line. That does **not** mean every `run_finished` has `"success": true` or that HTTP succeeded every turn.

**Qualitative findings from that batch’s `run.jsonl` files (categories, not guaranteed current):**

| Category | Example signals in traces | Fixed by layout/script only? |
|----------|---------------------------|------------------------------|
| HTTP 429 (TPM) | `run_finished` / transport errors mentioning rate limit | No — needs backoff, model tier, spacing runs |
| HTTP timeout / 400 body | Timeouts; invalid JSON body | No — transport / request builder |
| Harness policy | `Action-intent turn ended without tools…`, max rounds | Partially — prompts + harness settings |
| Tool `success: false` | MI vs base material, missing `query`, bad paths, etc. | Partially — dispatch + catalog + prompts (see B) |

Anything in **Section A** is a **snapshot**. It does **not** automatically reflect the plugin DLL you have **after** later builds unless you **re-run** the harness.

### Current runner (how to avoid confusion next time)

The script [`tests/long-running-tests/run-long-running-headed.ps1`](../tests/long-running-tests/run-long-running-headed.ps1) now:

- Creates **one** folder per invocation: `\<scenario-folder>\runs\run_<local_yyyyMMdd-HHmmss_fff>\` (local machine time, millisecond resolution).
- **Does not delete** previous suite output; older batches remain on disk.
- Writes **`last-suite-summary.json`** inside that batch folder **and** overwrites `\<scenario-folder>\last-suite-summary.json` as the latest pointer.
- Unless **`-SkipClassification`**, runs [`tests/classify_harness_run_jsonl.py`](../tests/classify_harness_run_jsonl.py) on the batch folder and writes **`harness-classification.json`** (rate_limit / http_timeout / harness_policy / tool_finish_false / etc.).
- Sets **`UNREAL_AI_HTTP_429_MAX_ATTEMPTS=6`** by default (was 2) so long batches align with transport backoff in [`FOpenAiCompatibleHttpTransport.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Transport/FOpenAiCompatibleHttpTransport.cpp).

**Rule:** After a new run, treat **`batch_output_folder`** / **`batch_stamp`** in `last-suite-summary.json` as canonical. If you see `run_root` paths under `...\runs\run_...\fine-tune-...__suite\`, you are on the **new** layout. If you see `...\runs\fine-tune-...__suite\run_20260328-184159\`, you are still looking at **old** layout data (or a copy).

**Manual classification:** `python tests/classify_harness_run_jsonl.py --from-summary tests/long-running-tests/last-suite-summary.json`

## B. Resolver / catalog / prompt ROI — what the codebase already does

These are **implemented in source** (verify with your build). They address **deterministic** tool-arg and contract issues; they do **not** remove 429s.

| Item | Location / behavior |
|------|---------------------|
| Content asset vs level selection | [`UnrealAiToolDispatch_EditorUi.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_EditorUi.cpp) — `ErrorContentAssetVersusSelection`, zero resolved actors with `/Game/...` style paths |
| MI vs base `UMaterial` for scalar/vector | [`UnrealAiToolDispatch_AssetsMaterials.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_AssetsMaterials.cpp) — `ErrorWithSuggestedCall` when `LoadObject<UMaterial>` succeeds |
| `asset_index_fuzzy_search` missing or empty query | [`UnrealAiToolDispatch_Search.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch_Search.cpp) — coerces to default query + `query_coerced` (same as empty string; **including omitted `query` key**). Catalog: [`UnrealAiToolCatalog.json`](../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json) entry no longer lists `query` as JSON Schema `required` (runtime always supplies a default). |
| Global invariants (short) | [`prompts/chunks/04-tool-calling-contract.md`](../Plugins/UnrealAiEditor/prompts/chunks/04-tool-calling-contract.md) — selection vs assets, MI, empty filters |

**Transport:** [`FOpenAiCompatibleHttpTransport.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Transport/FOpenAiCompatibleHttpTransport.cpp) already implements 429 wait + retry (up to `UNREAL_AI_HTTP_429_MAX_ATTEMPTS`, max 8). The headed script now defaults attempts to **6** so long runs do not override down to 2.

## C. Implemented follow-up (this repo)

| Plan item | Implementation |
|-----------|------------------|
| Classify failures | [`tests/classify_harness_run_jsonl.py`](../tests/classify_harness_run_jsonl.py); optional auto-run from `run-long-running-headed.ps1` → `harness-classification.json` |
| 429 backoff for long runs | Default `UNREAL_AI_HTTP_429_MAX_ATTEMPTS=6` in headed script |
| `asset_index_fuzzy_search` missing `query` | Dispatch coercion + catalog `required` removed; summary text updated |
| Validate with new batch | Build editor, run headed script, read `batch_output_folder` + `harness-classification.json` |

**Optional (not done here):** extra C++ automation tests for `ErrorWithSuggestedCall` JSON shapes — long-running JSONL remains the integration surface.

## D. What this document does **not** claim

- It does **not** assert that batch `20260328-184159` ran against the **current** Git SHA or DLL.
- It does **not** equate `failed_suite_count == 0` with “model quality was good.”
- It does **not** list every `tool_finish` failure in that batch; re-run grep on the specific `run.jsonl` paths under `run_root` if you need exact counts.

When in doubt: **re-run the harness** and use the **new** `runs\run_<local>\...` folder + `last-suite-summary.json` fields as the single source of truth for “latest results.”
