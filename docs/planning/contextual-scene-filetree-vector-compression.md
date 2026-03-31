# Contextual scene + file-tree knowledge: vector relevance + compression (proposal)
This document proposes an approach for storing and selecting "scene info" and "file tree info" so the model sees the right context without constantly paying the full context cost.

## TL;DR
Yes, it is smart directionally: treat both scene- and project-structure-derived knowledge as a set of *chunks* with embeddings + metadata, use vector similarity to rank relevance per query, and incrementally reduce (or compress) low-utility chunks over time.

## Motivation
Scene info and file tree info are both:
- structured-but-too-large to send verbatim every turn
- highly redundant (many assets share names / similar metadata)
- query-referential (the first model query often "points" to what should be expanded next)

The failure mode we want to avoid is "ever-growing context" where early irrelevant chunks persist and crowd out the few chunks that actually matter for later steps.

## Core idea
### 1) Represent knowledge as chunks, not monoliths
Break both inputs into small, retrievable units:
- **Scene chunks:** per actor / per component / per blueprint instance, optionally grouped by class or folder
- **File-tree chunks:** per directory, per file, and for large files by region (or by "symbol-sized" summaries)

Each chunk should carry:
- raw text (or a pointer to it)
- stable identifiers (asset path, object path, package path, etc.)
- metadata (type: actor/material/blueprint/file; tags; dependencies if known)
- an embedding (for vector retrieval)

### 2) Use vector similarity to rank "what is likely to be referenced"
When the model issues a new user goal or follow-up request:
- embed the request (or the model's "intent-rewriting" query)
- retrieve the top-K scene chunks and top-K file-tree chunks by vector similarity
- optionally rerank by metadata alignment (domain tags like "viewport", "materials", "blueprints", "asset editor")

This makes relevance "query-driven" instead of "always include everything".

### 3) Use compression to keep space bounded
Compression should be *meaning-preserving enough* for the model to navigate relations, while allowing the system to discard or summarize redundancy:
- **Clustering/dedup by name similarity:** group many similarly named assets into one "equivalence class" summary (e.g., `M_*_Glass*`, `BP_*_Door*`)
- **Tiered representations:** store chunk levels like:
  - level 0: tiny summary + key attributes
  - level 1: expanded summary (key fields, dependencies, notable references)
  - level 2: near-raw text for only the highest-ranked chunks this turn
- **Relation-focused summaries:** if we can extract edges (asset A depends on B, references, usage, inheritance), then compress by graph neighborhoods rather than raw dumps
- **Budget-aware truncation:** if context budget is tight, prefer keeping high-connectivity chunks (graph hubs) over purely textual variety

### 4) Incremental relevancy: start broad, then "decay" low-utility chunks
Start with a larger slice because early turns often establish references that later turns need.
Then, for subsequent turns/sessions:
- track which chunks actually get selected and/or cited by the model (implicit: retrieval hits; explicit: tool usage; or mention detection)
- compute a per-chunk "utility score" (e.g., retrieval frequency * success proxy)
- lower the chunk's effective score / retrieval weight as it becomes less used

You can implement this as:
- a multiplier on the vector score (vector score * utility multiplier)
- or a "relevance cutoff" that gradually prunes the candidate set

Once the system has evidence that a chunk isn't being referenced, it can be demoted to a compressed form or omitted.

## Should we also store "how assets relate to each other"?
Likely yes, but in a way that does not explode implementation complexity:
- Use vector similarity to find candidate chunks.
- If you can cheaply extract dependencies/edges, use them to *explain* and *expand*:
  - "You're looking at material X; here are the assets that reference it"
  - "You're editing blueprint Y; here is the class hierarchy + related assets"

A good MVP is not necessarily a full relational graph; it can be "weak edges" derived from:
- asset registry references (if available)
- naming/path conventions (e.g., folders by system)
- simple parsing of scene components/properties that point to assets

## Data model sketch (MVP-friendly)
For each knowledge chunk `C` store:
- `chunk_id` (stable)
- `source` (scene | file_tree)
- `entity_type` (actor | component | material | blueprint | file | directory)
- `entity_id` (stable id used for utility + dedup) (asset path / object path / file path / directory path)
- `canonical_id` (human/semantic canonical) (asset path / object path / file path)
- `representation_level` (L0 | L1 | L2) (how compressed the payload should be when placed in context)
- `summary_text` (short; always available)
- `expanded_text` (optional; stored or lazily fetched)
- `embedding` (optional; only when indexable in vector store)
- `tags` (type/domain keywords)
- `edges` (optional adjacency list)
- `utility_score` (runtime-updated)

For relation expansion:
- a function `expand(C, query_context)` returns additional neighbor chunks within a small radius and/or with a minimum edge weight.

## Relevancy update strategy (practical considerations)
The naive approach "remove anything not selected last turn" can be too brittle.
Instead:
- use exponential decay for utility with explicit rewards/penalties:
  - `utility = utility * decay + hit_reward - budget_drop_penalty`
  - initial defaults (tunable): `decay=0.85`, `hit_reward=1.0`, `budget_drop_penalty=0.25`
- compute "hit_reward" as: the chunk (or its `entity_id`) was included in the final context AND it was referenced by at least one subsequent tool-call argument (matches by `/Game/...` paths or the canonical id).
- compute "budget_drop_penalty" as: the chunk was retrieved but dropped from the final context due to budget/caps (not because it was irrelevant/safety).
- keep a minimum "long tail" set so we can recover when intent shifts:
  - long-tail floor: always keep top 8 directory families + top 4 asset-equivalence clusters at L0 even if their utility is low.

## Head selection: how we identify the small set that matters most
We want a deterministic way to say "this small subset of entities explains most of the useful context," so we can compress the long tail more aggressively.

### 1) Utility signals per `entity_id`
For each `entity_id` (folder family, asset family, or specific asset) we track:
- `retrieval_hit_count`: how often the entity (or a chunk mapping to it) appears in retrieval results.
- `kept_count`: how often its chunks survive ranking and get packed into the final context.
- `budget_drop_count`: how often its chunks are dropped due to caps/budget (not policy/safety).
- `action_ref_count`: how often its `canonical_id` or a matching `/Game/...` path appears in tool arguments after it was in context.

These fold into the same `utility_score` as above:
- `utility = utility * decay + w_keep * kept + w_action * action_ref - w_budget * budget_drop`
- initial weights (tunable): `w_keep=1.0`, `w_action=1.5`, `w_budget=0.25`.

### 2) Defining the "head set" deterministically
Periodically (or on demand) we compute:
- a sorted list of `entity_id`s by `utility_score` (descending).
- cumulative coverage metrics over the sorted list, for example:
  - `% of total kept_count` explained by the prefix,
  - `% of total action_ref_count` explained by the prefix.

We then define the **head set** as:
- either the smallest prefix that explains at least `X%` of `kept_count` (e.g. 80%), or
- a fixed top `N` (e.g. top 1–2% of entities) when the distribution is flatter.

This is deterministic because it relies only on stored counts and a fixed ordering, not on LLM decisions at selection time.

### 3) Using the head set to guide compression
Once we have a head set, compression rules become:
- **Head entities:**
  - always keep an `L0` representation available,
  - allow promotion to `L1`/`L2` more readily (lower upgrade thresholds),
  - when summarizing folders like "Nature Assets" with thousands of meshes, prefer head meshes as exemplars.
- **Tail entities:**
  - keep only `L0` or clustered `L0` summaries,
  - upgrade to `L1`/`L2` only when explicitly targeted by retrieval + current intent.

### 4) Folder-level deterministic compression by name/metadata only (e.g., "Nature Assets")
We will not read or embed raw binary asset data for this; compression is based purely on names and metadata (paths/classes) that we can get from the Asset Registry.

For very large homogenous groups (like a folder `Nature Assets` with many similar static meshes):
- Compute a **group key** using only registry metadata:
  - `(normalized_parent_folder_path, asset_class, optional equivalence_class_id_from_name)`.
- Determine membership deterministically:
  - list assets under that folder via Asset Registry,
  - filter by class (e.g., `StaticMesh`),
  - optionally compute `equivalence_class_id` from normalized basenames (lowercase, strip common suffixes, tokenize).
- Sort members by a stable key (e.g. soft object path string).
- Pick exemplars deterministically, still name-only:
  - `first`, `median`, `last` (or `first 3`) so we do not bias toward only one naming cluster.
- Emit a compressed `L0`/`L1` summary, again based only on names/paths:
  - folder/group description (e.g. "`Nature Assets` static meshes"),
  - asset class,
  - `count`,
  - exemplar asset paths,
  - a stable `entity_id` for expansion.

When the model later needs more detail, it can:
- request expansion for that `entity_id`, or
- call a deterministic tool that enumerates members under the same group key (folder + class + optional equivalence class),
and the system can enumerate the full set or a filtered subset — still without ever reading raw mesh file contents.

## Start-broad then shrink (your proposed mechanism)
Yes - directionally this is the right control loop:

### 1) Seed policy (early turn)
On the first turn of a new request/thread, include a *larger* candidate set than you would on later turns:
- For file-tree (vector-backed) candidates: use `K_seed = min(12, 2 * MaxSnippetsPerTurn)`.
  - In the current code, `MaxSnippetsPerTurn` defaults to 6, so seed is 12 (then capped by the context ranker).
- For scene (non-vector) candidates: rely on existing deterministic anchors (selected actors, open editors, Content Browser selection, explicit attachments) and do not try to "vector-demote" them.
- Prefer summaries over raw text so token cost stays bounded.
 - Do *not* wait for the model to first "discover" a target via a tool; the seed is meant to reduce that initial misstep failure mode.

### 2) Demotion policy (later turns)
As turns progress, demote chunks that appear to be "least relevant" to the changing query intent:
- For each candidate chunk `C`, compute an effective score like:
  - `effective_score = vector_similarity(C, current_intent_query) * utility_multiplier * scope_multiplier`
  - where `utility_multiplier` decays when the chunk stops being selected/cited.
- initial utility_multiplier mapping:
  - if `utility_score < 0.15` => `utility_multiplier = 0.2`
  - else `utility_multiplier = clamp(utility_score, 0.0, 1.0)`
- Additionally, you can explicitly prune using *vector separation*:
  - if `C` is far below the top-K leader similarity for multiple consecutive turns, treat it as stale.
- When demoting, do not necessarily drop completely:
  - compress `C` down one representation level (L2 -> L1 -> L0) instead of deleting it.

### 3) Long-tail guardrail (recovery)
Always keep:
- a small long-tail set of compressed clusters (scene clutter groups, file-family groups, likely editor panes)
- plus any chunks that have high "connectivity" (e.g., dependencies, references, or "this is where edits usually happen" anchors)

This avoids the brittleness of "prune by last selected only".

## Repo alignment (what is already implemented here)
- The plugin already has optional local vector retrieval that indexes *text corpora* (filesystem text chunks, optional Asset Registry shards, optional Blueprint feature records). It does not index live editor snapshots or raw binary `.uasset` bytes.
- Retrieval results are returned as bounded `retrieval_snippet` candidates, merged into the unified context candidate ranker under strict per-turn caps and budgets. Retrieval is disabled by default and must degrade to deterministic-only context.
- Retrieval query is "embedding-first" (cosine Top-K in SQLite) with a lexical fallback, and snippet payloads are taken from the stored chunk text.

Given that, for "scene info + file tree info" in v1:
- Scene info stays deterministic (selected actors, open editors, Content Browser selection, explicit attachments, tool results).
- File tree info gets vector-backed relevance (via retrieval snippets + mapping to folder/asset families).
- Utility-based demotion/pruning is implemented at *context assembly time* by scaling retrieval snippet scores and by enforcing long-tail floors (not by deleting from the index).

## Prior art quick pointers (so we use established terminology)
- Contextual compression (filter/rerank/extract only relevant passages from retrieved docs): https://blog.langchain.com/improving-document-retrieval-with-contextual-compression
- VectorDB-focused contextual compression overview: https://mintlify.com/avnlp/vectordb/advanced/contextual-compression
- Hierarchical / parent-child RAG (small chunks for precision, larger context for completeness via retrieval triggers): https://arxiv.org/abs/2602.22225
- Semantic caching and long-tail behavior (utility/reuse signals for what to keep): https://arxiv.org/pdf/2510.26835

## Exact compression strategies we will use (v1)
### 1) Hierarchical representations for context injection (L0/L1/L2)
We will store three representation levels per "entity" and use them as a replacement policy under token pressure:
- `L0` (always-available): short structured summary (id, type, and 2-5 salient attributes like class/family, common tags, or representative asset paths).
- `L1` (mid detail): slightly longer summary including a small dependency/usage list (top 3-8 neighbors or references) but still non-verbose.
- `L2` (deep detail): near-raw chunk text only for the top 1-2 entities for the current turn (for example: file text chunk(s) or a blueprint feature block).

Representation selection rule under budget:
- Prefer `L0` first.
- Upgrade an entity from `L0` to `L1` if it is in the top 3 by (vector_similarity * utility_multiplier).
- Upgrade to `L2` only if budget remains after packing the current anchors (engine header, recent UI focus, explicit attachments) AND the user intent implies "inspect/verify/modify code/blueprint".

### 2) Name-based equivalence classes (dedup for similar assets)
To avoid blowing up context when many assets share similar names:
- Define an `equivalence_class_id` per asset family by normalizing its basename:
  - lowercase
  - strip common UE suffixes (e.g. `_C`, `_Inst`, `MI_`, `BP_` prefixes kept as family)
  - tokenize by `[A-Za-z0-9]+`
- Cluster assets into the same class if they share at least 60% of tokens AND have the same `entity_type` family (material_instance vs blueprint vs other).
- The stored cluster summary lists:
  - up to `N_examples=5` concrete asset paths
  - a small common attribute set (derived from the cluster members, truncated to fit L0/L1 budgets)

Vector index strategy for equivalence classes:
- Embed and retrieve primarily from the class summaries (`L0`/`L1`), not from every member asset.
- Only retrieve member-level `L2` when the user or tool result requires a concrete path (e.g., an `asset_path`).

### 3) Relation-neighborhood compression (cheap edges, bounded radius)
When we do have edges/links cheaply (from Asset Registry shards, blueprint feature extraction, or existing deterministic queries):
- At retrieval time, expand neighbors only within a fixed radius:
  - radius `R=1` default (direct references)
  - max neighbors per entity `N_neigh=8`
- Compress the neighborhood into a single `L1` block: "top references/consumers/inheritance chain" rather than listing every raw edge.

### 4) Budget-aware packing is the final safety net
Even with tiered representations, context packing is still governed by the existing unified ranker:
- retrieval snippets below `MinRetrievalCandidateScoreToPack` are dropped
- per-type caps cap how many retrieval snippets can enter the context
- a soft budget reserve prevents crowding out live chat/anchors

## Incremental relevancy updates (exact utility mechanics)
We will treat "least relevant" as "low-utility after repeatedly being retrieved/considered but not acted upon":
- On each context build:
  - for each packed retrieval snippet, increment its associated `entity_id` utility by `hit_reward=1.0`
  - for each retrieved snippet that was dropped due to budget/caps, apply `budget_drop_penalty=0.25`
  - for everything else, apply decay: `utility *= 0.85`
- If an entity's `utility_score < 0.15` for `stale_turns=3` consecutive turns, mark it stale:
  - stale entities keep `L0` in the long tail, but get `utility_multiplier=0.2` (strong demotion) so they rarely win upgrades.
- Never delete entities from the long tail:
  - stale only affects upgrade probability (L1/L2), not the existence of L0 anchors.

## Implementation plan (incremental, repo-aligned)
### Separation of concerns (architectural expectations)
This design must keep the same separation-of-concerns discipline as the rest of the plugin:

- **Retrieval service (`FUnrealAiRetrievalService` + `UnrealAiVectorIndexStore`)**
  - Owns: index lifecycle, chunking, embeddings, cosine/lexical Top‑K, `FUnrealAiRetrievalSnippet` creation.
  - Does not know about: prompt format, budgets, head selection, or L0/L1/L2 representation levels.
  - Interface remains: `Query(...) -> Snippets[]`, prefetch helpers, and index diagnostics.

- **Context service (`FUnrealAiContextService`)**
  - Owns: per-thread `FAgentContextState` (`context.json`), project-tree summaries, collection of candidates (attachments, tool results, snapshot fields, memory snippets, retrieval snippets).
  - Calls into: retrieval service (for snippets) and `UnrealAiContextCandidates::BuildUnifiedContext` (for scoring/packing).
  - Does not compute scores directly or re-implement packing; it sets budgets/options and consumes the result.

- **Context candidates + ranking (`UnrealAiContextCandidates.cpp` + `UnrealAiContextRankingPolicy.h`)**
  - Owns: candidate envelopes, feature extraction (mention, heuristic semantic, recency, etc.), scoring, packing under budget.
  - Also owns: application of `utility_multiplier` for retrieval candidates, the `contextAggression` mapping to fill/caps/gates, and any head-set logic that uses per-`entity_id` utility inputs.
  - Does not call retrieval directly or know how the vector index is implemented.

- **Utility/head-selection state**
  - Lives either in a small sidecar (e.g., a head-tracker helper) or as data in context state; it is a *data provider* to the ranker (for `utility_multiplier` and head/tail membership).
  - Does not own prompt strings or retrieval internals.

- **Prompt builder / harness**
  - Prompt builder (`UnrealAiPromptBuilder`, `UnrealAiTurnLlmRequestBuilder`) continues to treat the context block as an opaque string; it does not inspect or alter candidate-level head/tail decisions.
  - Harness (`FUnrealAiAgentHarness`) continues to run tool loops and record tool results; it only contributes indirect signals (like tool arguments referencing assets) that utility/head-selection may later use as inputs.

### Phase 0: instrumentation (no behavior change)
1. Extend context decision logs to include, for `retrieval_snippet` candidates:
   - snippet `SourceId` and a derived `entity_id` (folder family or asset family)
   - keep/drop reason (budget, min-score, per-type cap)
2. Add a small per-thread cache in memory (and later persistence) that can store utility for these `entity_id`s.

### Phase 1: file-tree relevance injection (vector-backed, scene deterministic)
1. Modify the project tree context blurb builder to incorporate retrieval snippets when retrieval is enabled:
   - map snippet `SourceId` (file path or `virtual://asset_registry/...`) to a directory family / asset family
   - choose top directory families to list as "Top folders" and top asset families to list as "Preferred create paths"
2. Keep the existing Asset Registry based defaults as a fallback when retrieval has no hits.

### Phase 2: utility-aware demotion (score scaling)
1. Compute `utility_multiplier` for retrieval candidates from the per-thread utility cache.
2. Apply the multiplier by either:
   - scaling the snippet score before it becomes `Features.VectorSimilarity`, or
   - adding a new ranking feature in the candidate envelope and updating the ranker formula in `UnrealAiContextCandidates.cpp`.
3. Enforce the long-tail floors and stale upgrade suppression rules.

### Phase 3: index-level compression upgrades (L0/L1 class summaries)
1. Add additional index-time corpora:
   - directory-level summaries (L0/L1) embedded as separate chunks
   - name-equivalence class summaries for assets (L0/L1) embedded separately
2. When retrieving, prefer class summaries; only retrieve member chunks when a tool requires a concrete path.

### Phase 4: relation neighborhood expansion (bounded radius)
1. When an `entity_id` is selected for `L1` upgrade, expand neighbors within `R=1` and `N_neigh=8`.
2. Compress expansions into a single structured L1 block to prevent edge-list blowups.

## Context aggression knob (debug/testing)
For tuning and diagnostics it is useful to have a single "context aggression" scalar in settings (0–1) that adjusts how full the context packer tries to be without changing the architecture.

Conceptually:
- `contextAggression = 0.0` → minimal extra context: conservative budgets, higher score gates.
- `contextAggression = 0.5` → current default behavior (unchanged from today's constants).
- `contextAggression = 1.0` → maximal safe context: looser score gates, more candidates packed (within global limits).

This knob should only affect *packing* and *gates*, not core ranking logic:
- Scale **soft fill** and candidate count:
  - `SoftContextFillFraction` moves between a low (e.g. 0.4) and high (e.g. 0.8) value based on `contextAggression`.
  - `MaxPackedCandidatesSoft` moves between a low (e.g. 16) and high (e.g. 48) range.
- Adjust **min score gates**:
  - `MinRetrievalCandidateScoreToPack` and `MinMemoryCandidateScoreToPack` can decrease slightly as `contextAggression` increases, letting more borderline-but-relevant snippets through during testing.
- Optionally scale **per-type caps** (lightly):
  - e.g. let `MaxRetrievalSnippet` grow from 4 → 8 across the range, staying within global budgets.

The intent is:
- **Debug/testing**: quickly see "too little vs too much" context without changing many constants.
- **Not** a user-facing correctness fix: if relevance/selection is fundamentally wrong, this should not be the band-aid.

## Risks / gotchas
- **Embedding churn and cost:** embedding large raw file-tree content can be expensive; prefer embeddings on summaries and hierarchical representations.
- **Staleness:** scene/file-tree may change during an interactive workflow; ensure you can invalidate or refresh affected chunks.
- **Over-pruning:** if the model's intent shift is abrupt, previously "irrelevant" chunks might be needed again. Preserve a small cache/long tail.
- **Hallucinated relevance:** vector similarity can pull semantically close but operationally irrelevant chunks. Mitigate via metadata reranking and utility signals.
- **Complex compression semantics:** "compressed" summaries must still include the facts the model needs to make correct decisions; don't compress away identifiers, dependencies, or action-relevant attributes.

## Proposed MVP (so you can validate the idea quickly)
1. File-tree relevance first: map existing `retrieval_snippet` SourceIds into folder/asset-family entries in the project-tree context blurb (scene stays deterministic).
2. Utility cache + demotion: update a per-thread `entity_id -> utility_score` from retrieval keep/drop decisions, then scale retrieval snippet scores by `utility_multiplier` during context ranking.
3. Index-time compression for L0/L1: add directory-level summaries and name-equivalence-class summaries as separate chunk types so vector hits favor compact representations.
4. Representation upgrade policy: implement entity-level dedup so context packing upgrades `L0 -> L1 -> L2` only when budget and intent gates allow it.
5. (Optional) bounded relation neighborhood: when upgrading an entity to `L1`, expand neighbors within `R=1` and compress into a single structured block.

## Testing plan: noisy project + single diagnostic query
To validate this design end-to-end before fully rolling it out:

1. **Pick a large, noisy open-source Unreal project**
   - Prefer one with:
     - dense `Content/` folders (e.g. `/Game/Nature`, `/Game/Props`, `/Game/Environment`),
     - a mix of Blueprints and assets so both file-tree and asset-folder compression matter.

2. **Implement a thin slice of this doc first**
   - Folder-level compression by name/metadata only:
     - group key `(folder, class, optional equivalence_class_id_from_name)`,
     - deterministic exemplars (`first`, `median`, `last` or `first 3`),
     - `L0` summary with `count` + exemplar paths + stable `entity_id`.
   - Utility logging only (no demotion yet):
     - record `retrieval_hit_count`, `kept_count`, `budget_drop_count` per `entity_id` into decision logs or a simple in-memory map.
   - Wire those summaries into the project-tree context blurb and retrieval snippets.

3. **Choose one or two focused diagnostic queries**
   Examples:
   - "Describe what's in my 'Nature Assets' folder and suggest three meshes to start a forest scene."
   - "Given this project, what are the main environment/prop asset families I should know about?"

4. **After each run, inspect three things**
   - **Context block**:
     - Does the project-tree section show compact folder summaries like  
       "`Nature Assets` static meshes: 1,000 assets (e.g. A, B, C, and 997 more similar meshes)"?
   - **Decision logs / traces**:
     - Which candidates were dropped (budget vs min-score vs caps)?
     - How many `retrieval_snippet` candidates survived vs were filtered?
   - **Qualitative behavior**:
     - Did the model have enough context to choose good assets / paths without needing raw enumeration?

5. **Iterate before enabling more advanced behavior**
   - Tweak:
     - folder equivalence heuristics (how we group), and
     - number of exemplars,
   - then gradually:
     - turn on `utility_score`-based demotion,
     - experiment with the `contextAggression` knob in debug builds to see how aggressive packing interacts with compression.

## Open questions for brainstorming
- How should we deterministically derive `entity_id` from current retrieval `SourceId` values (file path vs `virtual://asset_registry/...` vs blueprint feature asset paths)?
- Should equivalence-class token similarity thresholds be global (60%) or entity-type-specific (blueprint vs material_instance families)?
- What is the best intent gate for enabling `L2` upgrades (heuristic keyword rules vs "tool intent" signals)?
- Which relation-neighborhood expansion sources should we use first (asset registry synthetic shards vs blueprint feature extraction vs deterministic tool reads)?

