# Context subsystem (`Private/Context/`)

## Separation of concerns (four layers)

1. **Capture / state** — `FUnrealAiContextService`, `RefreshEditorSnapshotFromEngine`, `RecordToolResult`, `UnrealAiEditorContextQueries`, `AgentContextJson`. Owns `FAgentContextState` and persistence. Does not assemble final `ContextBlock` except by calling the unified builder.

2. **Ingestion** — `UnrealAiContextIngestion.cpp`: per-source functions append `FContextCandidateEnvelope` rows (`Payload`, `RenderedText`, `TokenCostEstimate`, optional `FFeatures` pre-fill). No custom total scores.

3. **Policy** — `UnrealAiContextRankingPolicy.h`, `FilterHardPolicy`, `ScoreCandidates`, `PackCandidatesUnderBudget` in `UnrealAiContextCandidates.cpp`.

4. **Orchestration** — `BuildUnifiedContext` → `CollectCandidates` → filter → score → pack.

## Rules

- The rolling model context body comes from **packed** candidate `RenderedText` only (joined in `BuildUnifiedContext`). Do not append extra prose in `BuildContextWindow` after the packer (except documented static system/developer prefixes outside the budgeted block).
- Harness autofill should read the same persisted signals as context (e.g. thread **working set**, editor snapshot), not a parallel relevance model.

## Post-refactor inventory

See `ContextDeprecationInventory.txt` in this folder for the last audited list. After large refactors, grep for `Block +=` on context strings outside `UnrealAiContextCandidates`, and for duplicate candidate formatting. Remove only symbols proven unused (build + tests).

## Related docs

- Repo: `docs/context/context-management.md`, `docs/planning/context-ingestion-refactor.md`
