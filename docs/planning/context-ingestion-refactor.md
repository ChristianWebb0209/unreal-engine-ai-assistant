# Context ingestion refactor — separation of concerns + working set

**Current authoritative plan** for editor **context** work (not planning/todo removal). Legacy planning cleanup: [planning-system-legacy-removal.md](planning-system-legacy-removal.md).

## Goals

1. **Separation of concerns** — Four layers: **Capture** (state, snapshot, persistence) → **Ingestion** (adapters that only append `FContextCandidateEnvelope`) → **Policy** (`UnrealAiContextRankingPolicy`, filter/score/pack) → **Orchestration** (`BuildUnifiedContext`). No model-facing `ContextBlock` prose assembled outside the packer (except documented `StaticSystemPrefix` exemption).

2. **Single candidate gate** — Everything that competes for the context budget enters `CollectCandidates` / ingestion units; **no** post-`BuildUnifiedContext` append (today: project tree blurb must move **into** the ranker).

3. **Thread working set** — Bounded MRU list of touched assets (tools, snapshot, attachments); emitted as **scored candidates**; same signals feed **harness autofill** (`blueprint_path`, etc.) in a consistent order.

4. **Code organization** — Split monolithic `UnrealAiContextCandidates::CollectCandidates` into named ingestion units (`UnrealAiContextIngestion_*` or `namespace Ingestion { void Append… }`).

5. **Documentation (final step)** — When implementation is done, update **all** relevant docs and the **architecture visual map** so they match the shipped design (see checklist below).

## Task checklist

| Step | Deliverable |
|------|-------------|
| 1 | `Context/README.md` (or equivalent) documenting the four layers and the “no append after pack” rule |
| 2 | Ingestion split: thin `CollectAllCandidates` + adapter units; `UnrealAiEditor.Build.cs` updated |
| 3 | Fold `UnrealAiProjectTreeSampler::BuildContextBlurb` into candidates; grep for any other `Block +=` after unified build |
| 4 | Policy: caps/base weights for new candidate types; scoring knobs stay in `UnrealAiContextRankingPolicy` + `ScoreCandidates` |
| 5 | `FThreadAssetWorkingEntry` + ring buffer on `FAgentContextState`; `AgentContextJson` schema bump |
| 6 | Working set updates: `RecordToolResult`, `RefreshEditorSnapshotFromEngine`, `AddAttachment`, optional tool-open hooks |
| 7 | Working set ingestion: envelopes + `EntityId` where applicable |
| 8 | `PopulateOpenEditorAssets` order: MRU merge with working set before snapshot → candidates |
| 9 | `TryAutoFillDispatchArgsFromContext`: resolve blueprint path from working set → snapshot → open editors → tool results |
|10 | Optional: Recent UI provenance; optional L1 one-line asset blurbs as separate candidates |
|11 | Prompt chunk nudge (“if snapshot/working set names the asset, skip redundant read tools”) |
|12 | Tests: `UnrealAiContextRankingPipelineTests`, autofill-without-prior-tool-results; `./build-editor.ps1 -Headless` |
| **13 (last)** | **Docs + architecture map sync** (mandatory before calling the effort complete) |

## Step 13 — Documentation and architecture map (last step)

Update the following so they **match shipped behavior** (remove “target” wording once code lands):

| Artifact | What to change |
|----------|----------------|
| [docs/context/context-management.md](../context/context-management.md) | Layered ingestion diagram; working set; project tree inside ranker; autofill; file pointers |
| [docs/architecture-maps/architecture.dsl](../architecture-maps/architecture.dsl) | `Context Service` container: ingestion adapters + working set + orchestration wording; refresh `BEGIN_README_MAP context-components` prose |
| [docs/README.md](../README.md) | Index row linking this plan |
| [docs/tooling/tool-registry.md](../tooling/tool-registry.md) | If context assembly narrative is referenced, one-line pointer to updated `context-management.md` |
| [docs/planning/planning-system-legacy-removal.md](planning-system-legacy-removal.md) | Keep cross-link to this doc (already) |
| Generated SVGs | After `architecture.dsl` edits, run [`scripts/export-architecture-maps.ps1`](../scripts/export-architecture-maps.ps1) (requires Structurizr CLI + PlantUML jar as documented in that script). If the toolchain is not installed locally, commit DSL + markdown updates first and regenerate SVGs in an environment that has the dependencies. |

## Verification

- Headless build passes.
- Context decision logs still useful under verbose build.
- Manual smoke: send with attachments, open assets, retrieval on/off; confirm `ContextBlock` includes expected sections and budget behavior.
