# Pitch: optimized tools catalog (schema-first, minimal selection churn)

This document proposes a **future tools catalog** shaped for **minimum prompt context** and **maximum correct `unreal_ai_dispatch` calls**, while **keeping the existing tool-selection pipeline unchanged**. Optimization is almost entirely **catalog JSON** (names, counts, summaries, `tags`, `tool_surface.domain_tags`, `parameters`, `context_selector`), plus handler routing that already exists behind `tool_id`.

**Companion:** [`tools-catalog-settings-unification.md`](tools-catalog-settings-unification.md) (unified settings / get-set patterns at the UE API layer).

---

## 1. How tool selection works today (deep but implementation-faithful)

The pipeline is **not** embedding-based in current C++. Retrieval is **BM25 over a synthetic document per tool**, plus **editor UI context bias**, plus an optional **usage prior**, then **tiered markdown** appended to the system message. The API still exposes a **single** native function when dispatch mode is on: **`unreal_ai_dispatch`**.

### 1.1 Where the LLM sees tools (`UnrealAiTurnLlmRequestBuilder`)

- **Default:** `UnrealAiRuntimeDefaults::ToolSurfaceUseDispatch == true` → the model receives **one** tool definition: `unreal_ai_dispatch` with loose `arguments` (`BuildUnrealAiDispatchToolsJson` in `UnrealAiToolCatalog.cpp`).
- The **per-tool JSON Schema** is **not** sent in the API `tools[]` array in dispatch mode. Instead, the **compact tool index** is appended to the **system message** as markdown: section `## Unreal tool index (\`unreal_ai_dispatch\`)` listing enabled tools.
- **Fallback:** If dispatch is off, `BuildLlmToolsJsonArrayForMode` embeds **each** tool’s full `parameters` object in `tools[]` (large upload).

Source: `UnrealAiTurnLlmRequestBuilder.cpp` (dispatch path + appendix append).

### 1.2 When tiered retrieval runs (`UnrealAiToolSurfacePipeline`)

`TryBuildTieredToolSurface` runs only when **all** of the following hold:

| Condition | Effect if false |
|-----------|-----------------|
| `UnrealAiRuntimeDefaults::ToolEligibilityTelemetryEnabled == true` | Returns immediately; **no** BM25 ranking (see §1.5). |
| `Catalog` loaded, `bWantDispatchSurface`, `Request.Mode == Agent` | Dispatch appendix still built via fallback. |
| `LlmRound == 1` | Later rounds in the same user send **reuse** the prior tool surface (no re-ranking). |

Query text: `Request.ContextComplexityUserText` if set, else `Request.UserText`.

### 1.3 Query shaping (`UnrealAiToolQueryShaper`)

Before BM25, the user string is optionally **rewritten** for retrieval:

- **Heuristic rules** prepend synthetic tokens for common intents (e.g. physics/collision → `query physics_trace …`; lit/unlit/wireframe → `modify viewport_rendering …`; verb/object pairs like `create blueprint`, `modify material`, `find asset`, `viewport_camera` vs `level_actor` vs `pie`).
- **Hybrid query** = `RawUserText + " " + Shaped` (duplicates raw text so original wording still influences token overlap).

This means **catalog text** that aligns with these **shaped keywords** (e.g. `viewport_rendering`, `physics_trace`, `blueprint`, `material`, `level_actor`) gets an easier BM25 match when the shaper fires.

Source: `UnrealAiToolQueryShaper.cpp`.

### 1.4 BM25 document text (`UnrealAiToolBm25Index`)

For each enabled tool, the index builds a **single string** (tokenized into words ≥2 chars):

```text
tool_id + " " + summary + " " + category + " " + tags[] + " " + tool_surface.domain_tags[]
```

**Not indexed today:** `parameters` property names, `failure_modes`, `ue_entry_points`, or `embedding_hint` (present in JSON but unused in `BuildDocText`).

Implications for catalog design:

- **`summary` is the main retrieval lever** after `tool_id` and `category`.
- **`tags` and `tool_surface.domain_tags`** should repeat **synonyms and task phrases** users actually type (and that the query shaper emits).
- Sparse or overly short summaries **hurt** ranking for borderline queries.

Source: `UnrealAiToolBm25Index.cpp` (`BuildDocText`).

### 1.5 Context bias (`UnrealAiToolContextBias`)

After BM25, each tool’s score is multiplied by **1.15** if any **tool domain tag** matches an **active** tag from **recent editor UI** (Blueprint graph, viewport, asset editor, etc.). If `tool_surface.domain_tags` is empty, **category** is mapped to a default tag (e.g. `blueprints` → `BlueprintGraph`, `world_actors` / `viewport_camera` / `selection_framing` → `Viewport`).

**Implication:** explicit `domain_tags` that match `GatherActiveDomainTags` output (`BlueprintGraph`, `Viewport`, `AssetEditor`, `PIE`) improve ranking when the user is already in that UI.

Source: `UnrealAiToolContextBias.cpp`.

### 1.6 Usage prior (`UnrealAiToolUsagePrior`)

If `ToolUsagePriorEnabled`, the combined score becomes:

`0.7 * (BM25_norm × context_mult) + 0.3 * operational_prior`

where `operational_prior` comes from **session-local** success/fail counts per `tool_id` (default 0.5 until enough data).

**Implication:** Renaming or merging `tool_id`s **resets** learned priors for that id; stable ids are valuable.

### 1.7 How many tools get into the appendix (`UnrealAiToolKPolicy`)

Among **non-guardrail** tools, sorted by score descending:

- `K` is chosen between `ToolKMin` (6) and `ToolKMax` (24) using **margin** between rank 1 and rank 2 scores (tight top match → smaller K).
- **Guardrail tools** (`context_selector.always_include_in_core_pack == true`) are **always** listed first (all of them), then the top `K` non-guardrail tools by score.

So the visible roster is **roughly** “all core-pack tools” + “K best matches,” not “top K overall” if core-pack is large.

### 1.8 Tiered appendix formatting (`BuildCompactToolIndexAppendixTiered`)

For the **ordered** tool list:

- The first `ToolExpandedCount` (default **4**) tools get an **expanded** block: summary + **Parameters (excerpt)** truncated to **900 characters** of JSON.
- Remaining tools are **one line**: `` `tool_id`: summary ``.
- Total markdown is capped by `ToolSurfaceBudgetChars` (default **80k**); if over budget, **non-guardrail** segments are dropped from the **end** until it fits.

**Implications:**

- **Top-ranked tools** get the only **full-ish** schema view; others must be chosen from **name + one-line summary**.
- **Verbose or duplicated parameter schemas** waste the 900-char excerpt and help the wrong tool.
- **Guardrail** tools are never dropped for budget (only non-guardrail entries are trimmed from the tail).

### 1.9 Core pack restriction (`ToolPackRestrictToCore`)

When `ToolPackRestrictToCore` is true, `ForEachEnabledToolForMode` only includes tools with:

- `always_include_in_core_pack == true`, **or**
- `tool_id` listed in `ToolPackExtraCommaSeparated`.

Everything else is **invisible** to BM25 and the appendix unless added to extras. This is a **strong** filter: catalog design must decide what is **always visible** vs **opt-in**.

Source: `UnrealAiToolCatalog.cpp` (`PassesModeAndPack`).

### 1.10 Fallback when tiered pipeline does not run

If `TryBuildTieredToolSurface` returns false (e.g. telemetry flag off, wrong mode, round ≠ 1), the builder calls `BuildCompactToolIndexAppendix`: **all** enabled tools in **sorted `tool_id` order**, **no** BM25 ordering, **no** expanded rows (expanded count 0 in that code path), huge char budget — essentially an **alphabetical wall of one-liners**.

**Takeaway:** Tiered mode is what makes retrieval matter; catalog design should still behave **adequately** if someone runs fallback (short summaries, unique `tool_id`s).

---

## 2. Optimization goals (aligned with the pipeline)

1. **Fewer, sharper tools** — fewer rows competing in BM25 and fewer confusable `tool_id`s.
2. **Retrieval-aligned text** — `summary` + `tags` + `domain_tags` that overlap **user language** and **query-shaper** vocabulary.
3. **Repeated argument patterns** — same property names across similar tools (`actor_path`, `confirm`, `asset_path`) so the model transfers skill without re-reading 900-char excerpts.
4. **Compact parameters** — prefer **discriminated unions** or **small enums** over dozens of optional boolean flags; keep excerpt under budget for the **top 4** expanded slots.
5. **Stable `tool_id`s** — preserve priors and muscle memory in prompts/docs.
6. **Core pack discipline** — only tools that are **always safe and generally needed** should be guardrail’d; specialized tools enter via **extras** or **good ranking**, not by default bloat.

---

## 3. Proposed catalog shape (schema-first, pattern-based)

### 3.1 Naming families (machine- and human-friendly)

| Pattern | Role | Example |
|---------|------|---------|
| `*_query` / `*_get` | Read-only, idempotent | `asset_registry_query`, `editor_get_selection` |
| `*_set` / `*_apply` | Mutations with clear pairing | `editor_set_selection`, `actor_set_transform` |
| `viewport_*` | Session viewport (camera, view mode, capture) | Keep one prefix for BM25 |
| `project_file_*` | Workspace files | Already consistent |
| `console_*` | Escape hatch for CVAR / exec | Keep narrow and dangerous-flagged |
| `setting_query` / `setting_apply` | **Future** unified settings envelope (see settings unification doc) | Single schema, `domain` + `key` |

**Pairing rule:** Wherever there is a `get`, the catalog summary should **name** the paired `set` in one sentence so one-liner tools still teach the contract.

### 3.2 Consolidation targets (examples)

These are **illustrative** merges — actual implementation requires dispatch router updates:

- **Viewport:** `viewport_set_view_mode` + related readbacks could become `viewport_state_get` / `viewport_state_set` with a small enum of facets (`view_mode`, `show_flag`, …) *if* handlers delegate cleanly.
- **Editor selection:** Already `editor_get_selection` / `editor_set_selection` — keep as the **pattern** for other domains.
- **Settings:** Prefer **one** `setting_query` + `setting_apply` with **typed** `domain` enum (see planning doc) instead of N small tools that differ only by INI section.
- **Assets:** Distinguish **registry / search** (`asset_registry_query`) from **mutations** (`asset_create`, import, save) so BM25 verbs (`find`, `create`) map cleanly.

### 3.3 JSON Schema discipline (for dispatch + excerpt)

- **`additionalProperties: false`** everywhere the API can enforce it — reduces invalid calls and repair turns.
- **Enums** for modes, view types, collision channels — **string tokens** that match UE UI language (`"Lit"`, `"Unlit"`, `"Wireframe"`).
- **Avoid** ten optional aliases for the same field in one tool; aliases help humans but **confuse** models and **inflate** excerpts. Prefer **one canonical name** + documented alias only in prompt chunks, not in schema.
- **Default** descriptions in `parameters.properties.*.description` should state **units** and **when to use** (retrieval text is not in BM25 for params, but the **expanded excerpt** is).

### 3.4 `tool_surface` and `context_selector`

- **`domain_tags`:** Set explicitly to match **UnrealAiToolContextBias** (e.g. `Viewport`, `BlueprintGraph`, `MaterialEditor`, `PIE`, `AssetEditor`) when the tool is **primarily** used in that UI; add **extra** tags for retrieval (`physics`, `collision`, `packaging`) if they are not in `category`/`tags`.
- **`always_include_in_core_pack`:** Reserve for **small, safe, high-frequency** tools. Too many guardrails **push** high-scoring tools out of the expanded top-4 slots and **inflate** the mandatory roster.
- **`priority` / `embedding_hint`:** Not consumed by current BM25 code; treat as **documentation only** unless a future indexer uses them.

### 3.5 Categories

Keep **stable category strings** for BM25 (`category` is in the doc text). Prefer **fewer categories** with clearer boundaries, or keep categories but ensure **summary/tags** carry the nuance (selection service does not sub-split by category beyond defaults in context bias).

---

## 4. What we do **not** need to change (per user request)

- **UnrealAiToolSurfacePipeline** scoring formula, K-policy, guardrail ordering, budget, round-1-only behavior.
- **UnrealAiToolBm25Index** tokenization (unless a separate initiative changes it).
- **UnrealAiTurnLlmRequestBuilder** dispatch vs native toggle.
- **Core pack** mechanism — only **which tools** are flagged `always_include_in_core_pack` and the **extras** list in `UnrealAiRuntimeDefaults.h` (that file is **not** “catalog only,” but it’s a small knob).

---

## 5. Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Merging tools makes **handler** complex | One router function per family; unit tests per `domain` / facet |
| BM25 can’t distinguish two merged tools | **Distinct `tool_id`** and **summary** keywords; avoid generic names like `editor_do` |
| Lost **expanded** slot for an important tool | Improve **ranking** (tags + shaper vocabulary) or **narrow** core pack |
| Renaming `tool_id` | Aliases in dispatch router for one release; update docs and priors |

---

## 6. Summary

The selection service is already **query-shaper + BM25 + UI bias + usage prior + K truncation + tiered markdown**. **Optimal catalog design** feeds that pipeline: **short unique `tool_id`s**, **keyword-rich summaries and tags**, **aligned `domain_tags`**, **consistent parameter shapes**, **fewer tools**, and **disciplined core-pack** membership. A **unified settings** pair and other **get/set families** fit this model **without** changing how tools are selected — only **what** is indexed and **what** appears in the appendix.
