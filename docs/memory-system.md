# Memory system (definitive)

This document defines the standalone memory subsystem for Unreal AI Editor. It is the single source of truth for memory schema, storage, lifecycle, extraction, pruning, and UI management.

## 1. Scope and non-goals

- **In scope now**
  - Persisted memory records under a dedicated local `memories/` namespace.
  - Memory CRUD/list/filter/delete flows via a dedicated memory service.
  - Hidden memory extraction/compaction pass that can produce zero memories.
  - Ranked memory retrieval integrated into context candidate assembly.
  - Settings UI for inspecting and deleting memory records.
- **Out of scope in this phase**
  - Provider-backed memory generation (v1 remains local compaction).

The memory system must remain isolated from chat transcript persistence and context persistence, with integration only through explicit service interfaces.

## 2. On-disk layout

Under `%LOCALAPPDATA%/UnrealAiEditor/`:

```text
memories/index.json
memories/items/<memory-id>.json
memories/tombstones.json
```

- `index.json`: lightweight searchable catalog.
- `items/<memory-id>.json`: full memory payload for each item.
- `tombstones.json`: deleted IDs with timestamps to reduce accidental regeneration loops.

## 3. Memory data model

## 3.1 Required fields

Each memory item requires:

- `id`: stable unique key (GUID or ULID string).
- `title`: concise scan key.
- `description`: short summary used for second-stage relevance checks.
- `body`: full detail payload.

## 3.2 Recommended metadata

- `tags`: deterministic labels for routing/filtering.
- `scope`: `project` or `thread`.
- `confidence`: `0..1` acceptance confidence.
- `createdAtUtc`, `updatedAtUtc`, `lastUsedAtUtc`.
- `useCount`.
- `sourceRefs`: evidence pointers (thread id, message ids, tool names, asset paths).
- `ttlDays`.
- `status`: `active`, `disabled`, or `archived`.

## 4. Retrieval contract (current)

Memory retrieval is staged and budget-aware:

1. **Title stage**: score by query overlap, tags, scope, confidence, and recency.
2. **Description stage**: expand only top-ranked candidates.
3. **Body stage**: expand only the final top-K for deep context.

This staged contract is used by the context ranker and keeps memory inclusion deterministic and token efficient.

### 4.1 Optional vector retrieval integration (post-v1)

When retrieval is enabled, the plugin may also index memory records into the local vector index and surface them as `retrieval_snippet` candidates. This does **not** replace the existing staged memory query contract:

- The deterministic memory service query remains the primary, budget-aware contract.
- Vectorized memory chunks are treated as an additional recall signal and must still respect ranking gates and budget caps.

See `docs/vector-db-implementation-plan.md` for the local vector index schema and migration behavior.

## 5. Extraction/compaction policy

The compactor runs as a hidden background workflow with strict gates:

- **Inputs**: thread `conversation.json`, bounded context artifacts, and tool-result outcomes.
- **Triggers**:
  - end-of-turn idle windows,
  - message-count thresholds,
  - explicit maintenance calls from UI/console.
- **Acceptance rules**:
  - memory must be actionable/reusable,
  - confidence must meet configured threshold,
  - duplicate checks against index/title+tag similarity,
  - discard low-value or stale candidates.

The extractor is allowed to produce no output for a run.

## 6. Retention and pruning

- Periodic pruning removes or archives:
  - expired memories (`ttlDays`),
  - low-confidence unused memories,
  - superseded duplicates.
- Deletes write to `tombstones.json` with deletion timestamps.
- Pruning must not mutate transcript/context artifacts.

## 7. Settings UX contract

Settings exposes a Memories panel with:

- searchable list (title, tags, confidence, updated timestamp),
- detail view (title, description, body, metadata, source refs),
- actions:
  - refresh,
  - delete,
  - optional disable/reactivate.

Settings toggles (stored in settings JSON):

- `memory.enabled`
- `memory.autoExtract`
- `memory.maxItems`
- `memory.minConfidence`
- `memory.retentionDays`

## 8. Service boundaries

The subsystem is isolated under `Private/Memory/` with its own:

- types,
- JSON serializers,
- service interface/implementation,
- ranking/filter logic,
- extractor/compactor logic.

Persistence stays behind `IUnrealAiPersistence` memory-specific methods. UI talks only to memory service APIs.

## 9. Deferred integration with context manager

Not implemented in this phase.

Future context integration will call a memory query API (for example `QueryRelevantMemories`) and ingest staged memory content under strict budgets:

- titles first,
- descriptions second,
- bodies last.

`IAgentContextService` will never read `memories/*.json` directly; it will consume only memory service results.
