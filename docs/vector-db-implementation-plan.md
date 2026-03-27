# Local Vector DB Implementation Plan

## 1) Purpose

Add an optional, local-only vector retrieval layer that improves context relevance for large projects while preserving existing deterministic behavior.

Normative language in this document follows RFC style:
- **MUST / MUST NOT** = required for a compliant implementation.
- **SHOULD / SHOULD NOT** = recommended unless there is a documented reason to differ.
- **MAY** = optional.

This design is intentionally lightweight:
- per-project index on local disk,
- background indexing with clear UI status,
- strict token budgets,
- hybrid ranking (existing heuristics + vector similarity),
- no product backend and no required cloud service beyond user-configured embedding API.

## 2) Alignment with current architecture

This plan extends existing systems documented in:
- `docs/context-management.md` (`IAgentContextService` pipeline and candidate ranking),
- `docs/memory-system.md` (staged retrieval and service isolation),
- `docs/agent-and-tool-requirements.md` (v1 no-vector constraint; this is post-v1 optional),
- `README.md` (local-first plugin architecture summary).

Current context assembly remains the source of truth. Vector retrieval is a new candidate source, not a replacement for:
- attachments,
- editor snapshot,
- tool outputs,
- mention parsing,
- memory service.

## 3) Core decisions

### 3.1 Scope
- **Decision**: Add vectors as an opt-in post-v1 feature.
- **Reason**: Preserve v1 deterministic behavior and avoid regressions.
- **Normative**:
  - Retrieval **MUST** be disabled by default for existing installs unless explicitly enabled.
  - When disabled, existing deterministic context behavior **MUST** remain unchanged.

### 3.2 Storage model
- **Decision**: Per-project index under local app data keyed by project id.
- **Reason**: Fast lookup, strict locality, no cross-project contamination.
- **Normative**:
  - Index persistence **MUST** be local-only and project-scoped.
  - Retrieval **MUST NOT** read chunks from a different `project_id`.

Proposed paths under `%LOCALAPPDATA%/UnrealAiEditor/`:
- `vector/<project_id>/index.db`
- `vector/<project_id>/manifest.json`
- `vector/<project_id>/jobs/` (optional diagnostics)

### 3.3 Retrieval model
- **Decision**: Hybrid ranking.
  - Keep deterministic relevance features from current ranker.
  - Add vector similarity as an additional signal for vector-capable candidate types.
- **Reason**: Better recall without sacrificing precision and safety.
- **Thread scope rule**:
  - Retrieval **MUST** strongly prefer current-thread material when available.
  - If a chunk/memory is explicitly tied to the active thread, ranker **MUST** apply deterministic scope boost constants.
  - Cross-thread/project-global results **MAY** appear, but **MUST** rank below equivalent in-thread hits unless in-thread coverage is empty.

### 3.4 Blueprint handling
- **Decision**: Index extracted Blueprint features (nodes/references/comments/variable tokens), not binary `.uasset`.
- **Reason**: Keeps implementation simple and semantically useful.

### 3.5 Reindexing strategy
- **Decision**: Hybrid freshness model.
  - Event-driven incremental updates (primary),
  - startup validation sweep (secondary),
  - periodic scrub (safety net),
  - lazy repair when stale hits detected (fallback).
- **Reason**: Reliable freshness with low overhead.

### 3.6 Formula/cap policy location
- **Decision**: Keep all retrieval formulas, thresholds, and caps in one unified policy location.
- **Reason**: Avoid drift, simplify tuning, and make behavior auditable.

Normative:
- `UnrealAiContextRankingPolicy` (or a dedicated adjacent retrieval policy file if this grows),
- with retrieval and non-retrieval caps visible in one place.
- all production formulas/weights/gates/caps **MUST** be defined in that single policy location.
- feature implementations **MUST NOT** introduce hidden per-call magic numbers outside that location.

## 4) Requirements

### 4.1 Functional
- Per-project vector index lifecycle:
  - create, load, query, incremental update, rebuild.
- Bottom-of-chat indexing status:
  - `Indexing project...` with spinner,
  - `Indexed`,
  - `Indexed (updating...)` when incremental jobs run.
- Query API for context service:
  - `Query(project_id, thread_id, query_text, options) -> retrieval_snippet[]`.
- Candidate integration:
  - vector snippets participate in rank/budget selection.

### 4.2 Non-functional
- Indexing work **MUST NOT** block editor/game thread.
- Query path **MUST** work offline after index exists (local retrieval only).
- Retrieval path **MUST** degrade safely:
  - if index unavailable/stale/corrupt, continue with existing deterministic context.
- Retrieval snippets **MUST** respect strict token/char caps.

### 4.3 Privacy/Security
- Index files **MUST** remain local-only.
- Index content **MUST NOT** contain secrets/API keys.
- Optional embedding API calls **MUST** use existing user-provided provider settings.
- Logs **SHOULD** redact raw indexed content where feasible.

## 5) Candidate types and vector applicability

### 5.1 Vector-capable candidates
- Code/document chunks from project files.
- Memory description/body chunks (optional second phase).
- Blueprint extracted feature chunks (textual feature records).

### 5.2 Non-vector candidates (stay deterministic)
- Editor snapshot fields (open tabs/selection/current folder).
- Immediate tool results from the current run.
- Explicit user attachments with direct references.

## 6) Data model

## 6.1 Chunk record
Current implementation stores chunks in a local SQLite DB (`index.db`) with:

- `chunk_id` (TEXT primary key)
- `project_id`
- `source_path` (file path, asset path, or `memory:<id>:thread:<token>`)
- `source_hash` (hash of the full source text/record used for incremental change detection)
- `chunk_text`
- `content_hash` (hash of `chunk_text`)
- `embedding_json` (JSON array wrapper; implementation detail)

## 6.2 Manifest
Current manifest (`manifest.json`) includes:

- `project_id`
- `index_version`
- `embedding_model`
- `last_full_scan_utc`
- `last_incremental_scan_utc`
- `files_indexed`
- `chunks_indexed`
- `pending_dirty_count`
- `status` (`ready`, `indexing`, `error`, `stale`)
- `migration_state` (`none`, `pending_reembed`, `reembedding`, `mixed_compat`)

## 7) Blueprint feature extraction

Generate stable, compact feature records per Blueprint:
- class/parent references,
- component classes,
- node op frequencies,
- referenced assets/classes/interfaces/functions,
- variable/function/comment keyword tokens.

Recommendations:
- Normalize and sort tokens for deterministic output.
- Exclude volatile IDs/coordinates from semantic text.
- Keep a concise canonical representation for embeddings.
- Enforce hard extraction guards:
  - max features per Blueprint (**MUST**),
  - max chars per feature record (**MUST**),
  - dedupe repeated tokens/signatures (**MUST**).

Implementation note: current extractor starts with a minimal, safe feature set from Asset Registry tags (asset path, parent class, generated class). This is intentionally conservative and can be expanded toward node/reference/token extraction in future iterations.

## 8) Indexing pipeline

### 8.1 Initial indexing
1. Resolve `project_id` (existing `Context/UnrealAiProjectId.*` utilities).
2. Build source inventory:
   - `Source/`, selected `Config/`, selected docs,
   - Blueprint extracted feature records.
3. Chunk + hash.
4. Embed batches.
5. Upsert into local index.
6. Mark manifest `ready`.

### 8.2 Incremental indexing
- Triggered by file/asset changes:
  - update changed chunks,
  - delete removed chunks,
  - refresh manifest counters/timestamps.

### 8.3 Corruption/rebuild handling
- If `index.db` fails integrity checks:
  - mark manifest `error`,
  - disable vector path for current run,
  - schedule full rebuild in background.

## 9) Reindexing policy

Use all four mechanisms:
- **Event-driven**: immediate incremental updates from known changes.
- **Startup sweep**: quick compare of hashes/mtime against manifest.
- **Periodic scrub**: low-priority consistency pass.
- **Lazy repair**: when retrieval returns stale/missing sources, enqueue targeted reindex.

Avoid time-only full rebuilds unless:
- embedding model changes,
- index schema version changes,
- corruption detected.

Embedding model migration policy:
- On model/version mismatch between query profile and manifest:
  - system **MUST** mark manifest `migration_state=pending_reembed`,
  - system **MAY** allow temporary mixed-mode querying only behind an explicit compatibility gate,
  - system **MUST** schedule background re-embed,
  - system **MUST** flip to `ready` only when migration completes.
- If compatibility is disabled, retrieval path **MUST** fail closed to deterministic-only context for that turn.

Current implementation adds a settings flag `retrieval.allowMixedModelCompatibility` (default false). When disabled, queries fail closed during model mismatch until a background re-embed rebuild completes.

## 10) Retrieval and ranking integration

## 10.1 Query flow per turn
1. Build retrieval query from user message + optional short thread summary.
2. Fetch top-k chunks from vector index.
Normative:
- Query construction **SHOULD** be deterministic for identical inputs.
- If a thread summary is used, it **MUST** be bounded and **SHOULD** use a deterministic/stable source.
3. Convert to `retrieval_snippet` candidates.
4. Merge into existing candidate list.
5. Rank with current policy + vector score feature.
6. Apply per-type and total token budgets.

## 10.2 Scoring extension
Extend `UnrealAiContextRankingPolicy` with:
- `vector_similarity_weight`,
- `vector_min_score_gate`,
- optional boosts for exact mention overlap on retrieved chunks.
- thread-scope boost/penalty constants for retrieval candidates.

Vector score **MUST NOT** bypass safety gates or token limits.

## 11) UI/UX changes

### 11.1 Chat status row
In chat tab/composer area:
- show index state for current project at the bottom:
  - spinner while indexing,
  - ready badge,
  - warning if stale/error.

### 11.2 Settings
Add retrieval settings in `SUnrealAiEditorSettingsTab`:
- `retrieval.enabled`
- `retrieval.embeddingModel`
- `retrieval.maxSnippetsPerTurn`
- `retrieval.maxSnippetTokens`
- `retrieval.autoIndexOnProjectOpen`
- `retrieval.periodicScrubMinutes`
- `retrieval.rebuildNow` action
- `retrieval.allowMixedModelCompatibility` (compat gate)

## 12) Codebase touchpoints

Primary integration points to update:
- Context pipeline and ranking:
  - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/FUnrealAiContextService.cpp`
  - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/FUnrealAiContextService.h`
  - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/UnrealAiContextCandidates.*`
  - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/UnrealAiContextRankingPolicy.h`
- Memory integration (optional phase 2 vectorization):
  - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Memory/FUnrealAiMemoryService.*`
  - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Memory/UnrealAiMemoryTypes.h`
- UI surfaces:
  - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorChatTab.*`
  - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorSettingsTab.*`

New module area (recommended):
- `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Retrieval/`
  - `IUnrealAiRetrievalService.h`
  - `FUnrealAiRetrievalService.*`
  - `UnrealAiVectorIndexStore.*`
  - `UnrealAiBlueprintFeatureExtractor.*`
  - `UnrealAiRetrievalTypes.*`

## 13) Persistence/API contracts

Add persistence methods to `IUnrealAiPersistence` for:
- loading/saving retrieval settings,
- reading/writing manifest,
- index health metadata if needed.

Keep retrieval index reads/writes encapsulated in retrieval service; context service should consume only retrieval query results.

## 14) Performance and budgets

- Background thread workers for indexing and embedding requests.
- Batched embeddings with adaptive batch sizes.
- Query path latency target:
  - local retrieval + candidate conversion under a small fixed budget per turn.
- Hard caps:
  - max retrieved chunks per turn,
  - max tokens contributed by retrieval snippets.

## 15) Observability

Add retrieval telemetry to local logs (no remote backend):
- index build start/end, chunk counts, duration,
- incremental update counts,
- retrieval query time and hit counts,
- stale-hit repair events.

Extend existing context decision logs to include:
- retrieved snippet IDs/scores,
- acceptance/rejection reason (budget, safety, low score).

Normative:
- These observability fields **MUST** be emitted when retrieval is enabled.
- Logging **MUST NOT** include secrets.

## 16) Rollout plan

### Phase 0: plumbing
- Define retrieval interfaces/types/settings.
- Add no-op retrieval service behind feature flag.

### Phase 1: local code/doc retrieval
- Build per-project index for code/docs only.
- Integrate retrieval snippets into context ranking and budgets.
- Add chat status indicator + settings toggles.

### Phase 2: Blueprint feature retrieval
- Add Blueprint feature extractor and indexing.
- Tune rank weights for Blueprint-heavy tasks.

### Phase 3: memory vectorization (optional)
- Add vector fields for memory retrieval stage.
- Preserve staged memory gates from `docs/memory-system.md`.

## 17) Testing plan

- Unit tests:
  - chunking determinism,
  - manifest transitions,
  - stale detection and targeted repair,
  - ranking behavior with/without vectors.
- Integration tests:
  - project open with missing index -> background indexing -> ready,
  - retrieval disabled fallback path,
  - corrupted index recovery path.
- Harness checks:
  - ensure input-token growth remains bounded via snippet caps.
  - verify thread-preferred retrieval outranks equivalent cross-thread matches.
  - verify migration-mode behavior (mixed vs disabled) matches policy.

Normative:
- A change **MUST NOT** ship unless unit + integration coverage exists for:
  - retrieval disabled fallback,
  - corruption recovery,
  - thread-scope ranking precedence,
  - model migration behavior.

## 18) Risks and mitigations

- **Risk**: Input token spikes from retrieval over-selection.
  - **Mitigation**: strict caps, min score gate, ranker penalties for redundant snippets.
- **Risk**: Stale index causes wrong context.
  - **Mitigation**: event-driven updates + startup sweep + lazy repair.
- **Risk**: UI confusion about readiness.
  - **Mitigation**: always-visible bottom status row with clear state text.
- **Risk**: complexity creep.
  - **Mitigation**: staged rollout and minimal first corpus.

## 19) Acceptance criteria

- Per-project index **MUST** be created and reused automatically.
- Chat **MUST** show indexing/ready state at bottom.
- Context assembly **MUST** use retrieval snippets only when retrieval is enabled and index is available.
- Deterministic context sources **MUST** continue to work unchanged when retrieval is disabled/unavailable.
- Incremental updates **MUST** keep index fresh without full rebuild on every run, except for documented rebuild triggers.
- Token usage **MUST** remain within configured retrieval + global caps.
- Retrieval p95 latency **MUST** be within configured local target.
- Stale-hit lazy repair **MUST** succeed for most stale detections under normal operation.
- Deterministic anchors (engine header + live editor anchors) **MUST** remain present under retrieval pressure.
- Migration behavior on embedding model/version mismatch **MUST** follow declared fail-open/closed policy.

## 20) Implementation checklist (execution order)

Implementation status note: this checklist is the original execution plan.
The current plugin has implemented core local vector indexing/retrieval flows; remaining unchecked items should be treated as follow-up hardening/tuning tasks, not evidence that retrieval is absent.

This checklist is designed for a single implementation agent to execute in order.

### 20.1 Phase 0: scaffolding and no-op wiring
- [ ] Create retrieval module area:
  - `Private/Retrieval/IUnrealAiRetrievalService.h`
  - `Private/Retrieval/FUnrealAiRetrievalService.*`
  - `Private/Retrieval/UnrealAiRetrievalTypes.*`
- [ ] Define no-op retrieval service that always returns empty results.
- [ ] Add retrieval settings persistence keys and defaults.
- [ ] Wire retrieval feature flag into context build path (disabled -> exact existing behavior).
- [ ] Add compile-only stubs for observability hooks.

### 20.2 Phase 1: local code/doc indexing and query
- [ ] Implement local index storage + manifest:
  - `Private/Retrieval/UnrealAiVectorIndexStore.*`
  - manifest fields from section 6.2.
- [ ] Implement initial indexing pipeline for code/docs:
  - inventory -> chunk -> hash -> embed -> upsert.
- [ ] Implement incremental update path (changed/deleted chunks).
- [ ] Implement corruption detection and fail-closed fallback to deterministic-only context.
- [ ] Add retrieval query API used by context pipeline.
- [ ] Add retrieval snippet candidate conversion in `UnrealAiContextCandidates.*`.
- [ ] Add scoring knobs and caps in unified policy location (`UnrealAiContextRankingPolicy` or adjacent policy file).
- [ ] Ensure no retrieval formula/cap constants are hardcoded outside policy location.

### 20.3 Phase 1.5: UI and settings
- [ ] Add bottom-of-chat index status row in `SUnrealAiEditorChatTab.*`.
- [ ] Add settings controls in `SUnrealAiEditorSettingsTab.*`:
  - enabled, model, per-turn snippet count/tokens, auto-index, scrub cadence, rebuild now.
- [ ] Ensure settings update retrieval behavior without requiring editor restart where feasible.

### 20.4 Phase 2: blueprint feature retrieval
- [ ] Implement `UnrealAiBlueprintFeatureExtractor.*`.
- [ ] Enforce required extraction guards:
  - max features per blueprint,
  - max chars per feature record,
  - dedupe repeated tokens/signatures.
- [ ] Index blueprint feature chunks with deterministic normalization.
- [ ] Add retrieval tests for blueprint-heavy projects.

### 20.5 Migration and thread-scope enforcement
- [ ] Implement model/version mismatch handling:
  - set `migration_state`,
  - compatibility gate behavior,
  - background re-embed scheduling,
  - ready-state transition only after completion.
- [ ] Implement thread-scope ranking precedence:
  - deterministic in-thread boost constants from unified policy.
- [ ] Validate cross-thread hits rank below equivalent in-thread hits unless in-thread coverage is empty.

### 20.6 Observability and diagnostics
- [ ] Emit retrieval telemetry:
  - build start/end, durations, counts,
  - query latency/hit counts,
  - stale-hit repair events.
- [ ] Extend context decision logs:
  - retrieved snippet IDs,
  - retrieval scores,
  - reject reasons (budget/safety/low-score).
- [ ] Redact sensitive/raw content in logs as required by section 4.3.

### 20.7 Test and rollout gates
- [ ] Unit tests:
  - chunk determinism,
  - manifest state transitions,
  - ranking with/without retrieval,
  - migration gating behavior.
- [ ] Integration tests:
  - missing index -> background index -> ready,
  - disabled retrieval fallback,
  - corruption recovery.
- [ ] Harness tests:
  - token growth bounded,
  - anchors preserved under pressure,
  - thread-scope precedence,
  - migration-mode behavior.
- [ ] Verify acceptance criteria in section 19 before enabling by default.

### 20.8 Default rollout recommendation
- [ ] Ship with retrieval disabled by default.
- [ ] Enable internally on selected projects.
- [ ] Tune policy constants in unified location.
- [ ] Re-run harness pressure suites.
- [ ] Flip default only after section 19 criteria remain stable across consecutive runs.
