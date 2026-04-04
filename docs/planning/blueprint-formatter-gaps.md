# Blueprint formatter gaps (competitive analysis + implementation list)

This document captures how our Blueprint formatter compares to the five reference plugins under `reference/`, why results can still feel weak in practice, and a concrete implementation list to close the gap toward consistently readable, “hand-authored looking” graphs.

**Related docs:** `docs/planning/blueprint-formatter-requirements.md` (target behavior), `reference/arch-analysis.md` (cross-plugin comparison on formatting, tooling, and creation).

---

## 1. Purpose

- Align engineering work with **observable competitor behavior** and **our own written spec**.
- Give a single place for **why** the formatter underwhelms on some graphs and **what** to build next.
- Provide an **implementation checklist** that can be ticked off in PRs.

---

## 2. Why our formatter can still feel bad

### 2.1 Specification versus implementation

`blueprint-formatter-requirements.md` describes a multi-pass engine: exec DAG analysis, lane assignment with branch separation, data-node attachment, crossing-aware reroutes, optional comment synthesis, and refactor suggestions. As of 2026-04, the plugin ships **BfsGrid** and **LayeredDag** in addition to **MultiStrand** / **SingleRow**, plus **presets**, **preserve_existing**, length/crossing-aware **data knots**, **auto region** comments (when comments mode allows), and **structured tool metrics**. Remaining delta vs the full spec is mostly **exec knots** (experimental flag), **golden-graph** test coverage depth, and **structured refactor suggestions** (not yet a tool surface).

### 2.2 Competitor narrative (Autonomix)

Autonomix publicly ties **T3D injection** to **Sugiyama-style DAG auto-layout** so every injected graph is normalized before the user evaluates it. Our workflow often **builds logic first** (IR, graph patch, T3D in builder mode) and **formats in a separate step**; if that step is conservative (`layout_scope: patched_nodes`) or the algorithm is not DAG-strong, the graph can look “AI messy” longer.

### 2.3 Transparency and trust

The UE LLM Toolkit exposes `layout_graph` results with **counts** (total nodes, laid out nodes, skipped, disconnected, data-only, entry points). If our tool surface returns minimal structure, it is harder to tune behavior or explain “what the formatter did,” which reads as low quality even when geometry improved.

### 2.4 Graphs that defeat simple heuristics

- **Multiple exec outputs** from one node (branch, sequence, switch) without dedicated vertical banding.
- **Reconverging** exec (later node fed by two branches) without explicit join placement rules.
- **Hub data nodes** feeding many consumers at different depths.
- **Loops** and **latent** chains (depth is not a simple DAG layer index).

These need explicit policies; overlap nudging alone does not produce Epic-style readability.

---

## 3. Reference plugins (under `reference/`)

Evidence is grounded in `reference/arch-analysis.md` and, where noted, readable source in this repo.

### 3.1 Autonomix (`reference/autonomix/Autonomix-main`)

**Formatting story:** Blueprint changes via **T3D injection**, then **auto-layout** described in README as **Sugiyama-style DAG** so graphs are “organized and human-readable.” Layout is part of **inject → layout → compile / verify**, not an optional afterthought.

**What we should learn:** Treat **full-graph layout as a first-class step** after large atomic graph writes; prioritize **layered DAG** semantics for exec flow.

### 3.2 UE LLM Toolkit (`reference/ue-llm-toolkit/ue-llm-toolkit-main`)

**Formatting story:** `FGraphLayoutHelper::LayoutGraph` in `GraphLayoutHelper.cpp` / `GraphLayoutHelper.h`:

- Finds **entry nodes** (events, custom events, function entry).
- **BFS** along **exec output** edges (`MakeK2ExecPolicy`).
- Assigns **depth** and **Y indices** per depth column; stacks separate entry subgraphs vertically.
- Places **pure data** nodes using **data output → consumer** links (`MakeDataConsumerFinder`), typically one column left of the **shallowest** consumer, with slot collision avoidance.
- **Configurable** `SpacingX`, `SpacingY`; **`bPreserveExisting`** skips nodes already non-zero positioned.
- **Anim Blueprint** graphs reuse the same helper with **pose link** policies.

**Tool surface:** `blueprint_modify` → `layout_graph` returns JSON with `total_nodes`, `layout_nodes`, `skipped_nodes`, `disconnected_nodes`, `data_only_nodes`, `entry_points` (see `MCPTool_BlueprintModify::ExecuteLayoutGraph`).

**What we should learn:** **Explicit, documented grid policy**; **preserve-existing** as a first-class option; **structured diagnostics** on every format call.

### 3.3 Ludus (`reference/ludus/`)

**Formatting story:** Public docs emphasize **spawning and connecting** nodes in the open Blueprint editor, not a published layout algorithm or deterministic formatter contract in the scraped documentation.

**What we should learn:** Ludus competes on **in-editor UX** and **context loading modes** (`Overview` … `Full`), not on an inspectable layout engine in this snapshot. Do not treat Ludus as the algorithmic baseline for DAG layout unless we obtain deeper implementation detail.

### 3.4 LLM Sandbox (`reference/llm-sandbox/llm-sandbox-unreal-main`)

**Formatting story:** No dedicated Blueprint graph formatter called out in the reference snapshot; focus is general agentic / Python execution.

**What we should learn:** Not a comparator for Blueprint beauty.

### 3.5 Aura (`reference/Aura`)

**Formatting story:** No accessible deterministic Blueprint EventGraph layout pipeline in sources; decompiled editor DLLs point at **Behavior Tree** comment specs, **material / mesh** tooling, and similar—not EventGraph layout (`arch-analysis.md`).

**What we should learn:** Not a comparator for Blueprint auto-layout in available evidence.

---

## 4. Our plugin (`Plugins/UnrealAiEditor`) — current strengths

These are real differentiators to preserve and extend:

- **Tool integration:** `blueprint_format_graph`, `blueprint_format_selection`, integration with `blueprint_apply_ir`, `blueprint_graph_patch`, `blueprint_graph_import_t3d`, and `blueprint_compile` (including format-on-compile options).
- **Layout scope:** Limit layout to **patched / materialized** regions versus **full graph**; **strong-layout promotion** after large IR applies (internal node-count threshold).
- **Algorithms:** **Layered DAG** only for exec layout (legacy strand/BFS paths removed).
- **Presets and knobs:** **Editor Preferences → Blueprint Formatting** (spacing density, knots, preserve-existing, reflow, comments) — not per-tool JSON; mapper sets `FUnrealBlueprintGraphFormatOptions`.
- **Post-layout:** Data knots (`ApplyWireKnots`), optional auto region comments, comment reflow; **`BlueprintCommentsMode`** respected.
- **Transparency:** Structured **`layout_*`** metrics on format / apply / patch responses; **`blueprint_get_graph_summary`** + **`include_layout_analysis`**.
- **IR layout hints:** When hints are non-zero and complete, respect explicit positions before post-passes (`LayoutAfterAiIrApply`).

---

## 5. Gap summary (what “super beautiful” requires)

Baseline before the 2026-04 formatter pass; see **§6.5** for what is implemented vs partial today.

| Area | Autonomix / spec expectation | UE LLM Toolkit | Our typical gap |
|------|------------------------------|----------------|-----------------|
| Exec structure | Layered DAG (Sugiyama-style), branch-aware | BFS depth grid | Strand / strip + overlap fix; not full crossing-aware layering |
| Data nodes | Placed from consumer graph | Column-left of shallowest consumer | Partially via strand logic; hubs and multi-consumer cases need clearer policy |
| Preserve manual layout | N/A in README | `preserve_existing` | Needs first-class, documented tool flag and behavior |
| Wire aesthetics | Implied by DAG + professional output | Minimal (no heavy knot story in helper) | Knots exist but not full crossing / length reroute policy from requirements |
| Transparency | Pipeline is “inject then layout” | Rich JSON counts | Expand structured formatter metrics in tool responses |
| Comments | Not detailed in competitor README | Not emphasized | Settings (`BlueprintCommentsMode`) vs formatter behavior must stay aligned |

---

## 6. Implementation list

Items are ordered roughly by **impact on perceived graph quality** and **foundation for later work**. Adjust order if product priorities shift.

### 6.1 Layout algorithms and policies

1. **Implement a DAG layer assignment pass for exec flow**  
   - Topological layering from entry nodes (events, custom events, function entry).  
   - Handle **branch fan-out** with **vertical bands** (`branch_vertical_gap` from requirements doc).  
   - Define **join nodes** where exec reconverges (place at max predecessor layer + stable Y rule).  
   - Document loop handling (max depth cap, or separate “loop body” band) to avoid infinite layer growth.

2. **Add an explicit layout profile: `BfsGrid` (Toolkit-aligned)**  
   - Optional strategy mirroring `FGraphLayoutHelper` semantics for quick readability and A/B comparison with `MultiStrand`.  
   - Reuse or share exec traversal and data-consumer placement logic where possible.

3. **Add an explicit layout profile: `LayeredDag` (Sugiyama-inspired)**  
   - Layers + crossing reduction (even a simple median heuristic beats none for wide graphs).  
   - Target parity with Autonomix’s **marketing bar** for injected graphs.

4. **Harden data-node placement**  
   - Primary consumer selection; **multi-consumer** = centroid or column between consumers.  
   - Collision-free slots in the data column (Toolkit-style `OccupiedSlots`).

5. **Wire quality pass (requirements doc §3.5)**  
   - Thresholds: `max_wire_length_before_reroute`, `max_crossings_per_segment` (even approximate).  
   - Insert **data knots** on orthogonal paths; keep **exec knots** behind a strict flag until invariants are proven.

### 6.2 Tooling, API, and settings

6. **First-class `preserve_existing` (or equivalent) on `blueprint_format_graph`**  
   - Match Toolkit behavior: skip nodes with non-zero positions when enabled, or support tagged “manual” regions per requirements doc.

7. **Structured formatter result object**  
   - Always return counts: nodes moved, knots added, comments adjusted, entry subgraphs, disconnected nodes, data-only nodes placed, skipped/preserved nodes.  
   - Align naming with existing internal metrics where possible.

8. **Named presets**  
   - **Superseded:** presets were folded into **spacing density** + bools on `UUnrealAiEditorSettings` (no `layout_preset` in tools).

9. **Align `BlueprintCommentsMode` with formatter**  
   - `Off`: no new comment boxes; only resize existing where needed.  
   - `Minimal` / `Verbose`: per requirements doc §8.3.

### 6.3 Analysis, testing, and pipeline integration

10. **Read-only graph analysis API**  
    - Emit: entry nodes, exec clusters, branch points, estimated complexity, optional crossing proxy.  
    - Usable by formatter internally and by LLM context exports.

11. **Idempotence and golden tests**  
    - Format twice → same positions (within tolerance for UE node size quirks).  
    - Snapshot tests for: linear chain, single branch, nested branch, sequence, switch, small loop.

12. **Post–big-bang apply: default strong layout**  
    - After large `blueprint_apply_ir` / T3D import / large patch, optionally run **`layered_dag` or full `multi_strand` + wire pass** once unless user disabled.

13. **Verification ladder (adjacent but high impact)**  
    - After format + compile: connection audit, orphan pins, duplicate GUIDs (where applicable)—see Autonomix-style verify story in `arch-analysis.md`.  
    - Makes “beautiful” graphs trustworthy, not just pretty.

### 6.4 Documentation and prompts

14. **Cross-link this doc from `blueprint-formatter-requirements.md` §7**  
    - Single narrative: spec = target, this doc = competitive gap + execution checklist.

15. **Prompt / tool descriptions**  
    - Document when to use `layout_scope: patched_nodes` vs full graph, and that **layout tuning** is **Editor Preferences**, not tool JSON.

### 6.5 Checklist status (2026-04-04)

| # | Item | Status |
|---|------|--------|
| 1 | Layered DAG exec (layers, branches, joins, loop policy) | **Done** (`LayeredDag` + `BlueprintGraphLayeredDagLayout`) |
| 2 | `BfsGrid` profile | **Removed** (unified layered DAG only) |
| 3 | Sugiyama-inspired crossing reduction | **Done** (median sweeps in layered layout) |
| 4 | Data-node placement | **Done** (shared consumer / slot logic in BFS + layered) |
| 5 | Wire quality (length / crossing thresholds, exec knots) | **Partial** — data knots + thresholds **done**; **exec knots** still behind `bAllowExecKnots` (default off), not fully proven |
| 6 | `preserve_existing` | **Done** (settings: **Preserve existing node positions**) |
| 7 | Structured formatter result / tool metrics | **Done** |
| 8 | Named presets | **Superseded** (spacing density + settings mapper) |
| 9 | `BlueprintCommentsMode` vs formatter | **Done** (auto regions + reflow) |
| 10 | Read-only layout analysis | **Done** (`include_layout_analysis` on graph summary) |
| 11 | Idempotence / golden graph tests | **Partial** — policy/JSON tests + **Editor** idempotence smoke on transient BP; full golden asset matrix still optional |
| 12 | Strong layout after large apply / T3D | **Done** (internal threshold; T3D post-format) |
| 13 | Verify ladder | **Partial** — added `dead_exec_outputs`, `pin_type_mismatch`; further steps as needed |
| 14–15 | Docs / prompts | **Done** (this doc, requirements §7, `arch-analysis`, `new-tooling-system`, `04-tool-calling-contract`, builder chunks, tools README) |

---

## 7. Success criteria (short)

- **Visual:** Branchy graphs show clear **columns** and **separated branches** without spaghetti overlap.  
- **Behavioral:** `preserve_existing` prevents regressions on hand-tuned areas.  
- **Operational:** Every format call returns **actionable metrics**; idempotence tests pass on golden graphs.  
- **Pipeline:** Large graph imports trigger a **single strong layout** by default.

---

## 8. Revision history

- **2026-04-04:** Initial version (competitive analysis from `reference/` + internal requirements; implementation checklist).
- **2026-04-04 (implementation pass):** Shipped BFS grid + layered DAG strategies, layout presets, `preserve_existing`, extended `FUnrealBlueprintGraphFormatResult` metrics on tools, length/crossing‑aware data knots, auto region comments (Minimal/Verbose), IR strong full‑graph promotion + `force_strong_layout`, T3D post‑import format, `blueprint_get_graph_summary` layout analysis, verify steps `dead_exec_outputs` / `pin_type_mismatch`, editor settings for default preserve + threshold, catalog + docs updates. Deferred: golden graph idempotence tests on assets, exec knots, structured refactor suggestions JSON.

