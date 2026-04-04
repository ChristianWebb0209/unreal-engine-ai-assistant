# T3D authoring and placeholder GUIDs (Autonomix-style)

Do **not** invent raw Unreal `FGuid` literals. The engine resolves **placeholder tokens** to real GUIDs during import.

## Token grammar

- Use **`__UAI_G_000001__`**, **`__UAI_G_000002__`**, … — six-digit zero-padded decimal index inside double underscores and `UAI_G_` prefix.
- Each distinct node or wire identity in your authored T3D should use a **fresh** token; reuse the same token only when the format requires the **same** identity (e.g. two references to one node).
- Invalid or malformed tokens are rejected with a structured tool error before import.

## Recommended loop

1. **`blueprint_graph_introspect`** — pin audit + node GUID map for the target graph.
2. **`blueprint_export_graph_t3d`** — read canonical T3D for the graph (or subgraph); respect `max_chars` truncation warnings.
3. Edit or generate T3D locally (or in-model), replacing real GUIDs you do not want to carry with **`__UAI_G_NNNNNN__`** as needed.
4. **`blueprint_t3d_preflight_validate`** — reflection-oriented checks **before** mutating the graph.
5. **`blueprint_graph_import_t3d`** — single atomic `ImportNodesFromText` apply (placeholders resolved server-side).
6. **`blueprint_compile`** then **`blueprint_verify_graph`** (see verification chunk).

## Pin links / wiring (critical)

`ImportNodesFromText` recreates **only what the T3D string encodes**. If you paste bare `Begin Object … K2Node_*` blocks without the same **`CustomProperties` / pin lines that reference other nodes** (as in a real `blueprint_export_graph_t3d` excerpt), Unreal will spawn **isolated, unwired** nodes.

- Prefer starting from **`blueprint_export_graph_t3d`**, then edit in place: keep **connection records** intact, only swap identities with **`__UAI_G_NNNNNN__`** where you need new GUIDs.
- Links reference **node GUIDs** (or your placeholders after resolution). A **distinct placeholder per node**, **reused** wherever that node’s GUID must match (including inside `LinkedTo=(…)` / pin wiring), matches export behavior.
- If T3D is awkward for wiring, use **`blueprint_apply_ir`** / **`blueprint_graph_patch`** `connect` ops instead of expecting magic from sparse T3D.

## When **not** to use T3D

If clipboard paste is unsafe (wrong graph, ambiguous scope) or the op is a tiny fix, fall back to **`blueprint_graph_patch`** or **`blueprint_apply_ir`** as documented elsewhere.

**Animation Blueprints:** **`ImportNodesFromText`** is aimed at **Kismet (script) graphs** (e.g. **EventGraph**). **AnimGraph** / state machine graphs often fail preflight—do not loop on T3D; switch to **`blueprint_graph_patch`** on **EventGraph** or document manual anim-graph steps. See **`kinds/anim_blueprint.md`**.

**Important:** `__UAI_G_*` tokens are **not** valid inside **`blueprint_graph_patch`** — use real GUIDs after introspect, or stay on the T3D import path. See **`06-cross-tool-identity.md`**.
