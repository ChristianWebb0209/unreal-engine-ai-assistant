## Blueprint Formatter Requirements (UE 5.7+)

This document outlines **algorithmic requirements** for an automatic Blueprint formatter that produces clean, readable graphs following common Unreal Engine best practices (Epic docs, community style guides, Allar’s UE style guide, etc.). Focus is on **deterministic inputs + parameters** so a machine can make decisions without human judgement.

---

## 1. Problem framing

- **Input model** (what the formatter sees):
  - Blueprint asset identifier (object path `/Game/...`).
  - For each **graph** (EventGraph, functions, macros, construction script, etc.):
    - Node set: class, pins, exec/data roles, default values, K2 GUIDs.
    - Edge set: connections between pins (exec + data).
    - Existing positions (X,Y), comment boxes, and reroute nodes.
    - Optional metadata: last compile result, custom categories, tags.
- **Output**:
  - New positions for nodes, reroutes, and comments.
  - Optional creation/removal of **comment boxes** and **reroute nodes**.
  - Optional creation of **CustomEvents / Functions / Macros** (refactor step).
  - No semantic change to compiled behavior (purely layout + factoring).
- **Constraints**:
  - **Preserve semantics**: pin wiring and node types must remain identical (except optional safe refactors like “Collapse to function” which are behavior‑preserving).
  - **Idempotent**: running formatter twice should yield the same layout.
  - **Stable under small edits**: minor node additions should cause local, not global, churn.

---

## 2. Global parameters / configuration knobs

These parameters control formatter behavior and should be explicit so the LLM / tooling can choose profiles, but the algorithm stays deterministic once configured.

- **Flow direction**:
  - `flow_direction`: `left_to_right` (default) or `top_to_bottom`.
- **Lane spacing**:
  - `exec_lane_dx`: horizontal spacing between major exec “columns”.
  - `exec_lane_dy`: vertical spacing between stacked nodes in one lane.
  - `data_lane_dx`: lateral offset for pure data/helper nodes relative to their exec anchor.
- **Branch separation**:
  - `branch_vertical_gap`: extra vertical distance between **diverging exec branches** from `Branch`, `Sequence`, `Switch`, etc.
  - `max_branch_fanout_before_macro`: numeric threshold beyond which we strongly recommend collapsing to macro / function instead of trying to layout 10+ branches inline.
- **Comment policy**:
  - `auto_comment_min_nodes`: min node count in a cluster before a comment box is injected.
  - `comment_style`: `none | light | verbose`.
  - `comment_title_from`: `event_name | function_name | heuristic_summary`.
- **Reroute policy**:
  - `max_wire_length_before_reroute`: screen‑space threshold after which we insert reroute nodes.
  - `max_crossings_per_segment`: try to reroute if a new wire would cross more than N others.
- **Refactor thresholds**:
  - `max_nodes_per_function`: if a function graph exceeds this, formatter can propose or perform a split (e.g. create new helper functions).
  - `max_nodes_per_event_cluster`: similar threshold inside an EventGraph for one contiguous exec subgraph.
- **Respect existing layout**:
  - `preserve_manual_regions`: list or tags for regions we never touch (e.g. around hand‑tuned timelines).
  - `preserve_comment_boxes`: whether to keep existing comment boxes and expand/shrink instead of recreating them.

---

## 3. Core passes / methods

Think of the formatter as a pipeline of deterministic passes that share a graph model.

### 3.1 Graph analysis pass

**Goal**: Derive structural information from the raw node/pin/edge set.

- **Identify entry points**:
  - Event nodes (`Event BeginPlay`, input events, delegates, CustomEvents).
  - Function entry / macro entry nodes.
- **Compute execution DAGs**:
  - Follow exec pins to build per‑entry **exec subgraphs**.
  - Mark split points (`Branch`, `Sequence`, `Switch`, loops) and joins (Exec pins that reconverge).
- **Cluster nodes**:
  - **Exec clusters**: nodes reachable from an entry until termination or call out.
  - **Data clusters**: pure nodes (no exec pins) feeding into those exec nodes.
  - **Side clusters**: debugging or logging subgraphs (e.g. `PrintString` only) that can be positioned consistently (e.g. at the bottom or near branch).
- **Complexity metrics**:
  - Node count per function / per exec cluster.
  - Max branching factor from any node.
  - Longest exec path length.
  - Node types histogram (heavy use of casts, timelines, latent calls, etc.).

### 3.2 Lane assignment (exec skeleton layout)

**Goal**: Assign each exec node to a **lane index** and an approximate Y ordering before precise coordinates.

- **Inputs**: exec DAG + branch structure + `flow_direction`, `exec_lane_dx`, `exec_lane_dy`.
- **Algorithm (left‑to‑right)**:
  1. **Topological order** each exec subgraph from its entry node.
  2. Assign `lane_x` as `depth` in the exec DAG (distance in edges from entry, with special rules for loops to avoid infinite depth).
  3. For each branch node:
     - Its outgoing exec pins form child lanes; assign incremental `lane_branch_id` and give each a vertical band.
     - Offset each branch’s base `y` by `branch_vertical_gap`.
  4. For nodes that reconverge, place them at the **max depth of their predecessors** and adjust Y to avoid collisions.

Result: a skeleton grid where each exec node gets `(lane_index, lane_row)` before translation into actual `(X,Y)`.

### 3.3 Data / helper node placement

**Goal**: Place pure data nodes near the exec nodes they support.

- For each **data node**:
  - Compute its **primary consumer** exec node (or small set of consumers).
  - Attach a virtual “data lane” relative to that exec node:
    - For left‑to‑right, place data nodes to the **left** of the exec lane, stacking vertically with `data_lane_dx` and `exec_lane_dy` spacing.
  - Ensure bounding boxes leave enough space for comment boxes.
- For shared data nodes (feeding multiple distant consumers):
  - Prefer a **central** position between the consumer exec nodes, then use reroutes (see 3.5) to prevent long crossing wires.

### 3.4 Comment box detection / synthesis

**Goal**: Maintain or inject **semantic regions** via comment boxes.

- **Existing comments**:
  - Expand/shrink to tightly wrap the nodes they already cover, with margin.
  - Keep their title text unless policy says otherwise.
- **Automatic comments** (if `comment_style != none`):
  - For each exec cluster or function:
    - If node count ≥ `auto_comment_min_nodes`, create a comment box with title derived from:
      - The entry node name (e.g. “Input: Jump — Movement Handling”), or
      - A simple heuristic summary (e.g. `Damage calculation and health update` from key functions and variables).
  - For patterns like:
    - Multiple sequential branches controlling related behavior (e.g. input handling), group under a single “Input Handling” comment box.

### 3.5 Wire tidy / reroute insertion

**Goal**: Reduce visual noise (spaghetti wires) without changing semantics.

- For each wire (exec or data):
  - Compute screen‑space **length** based on planned node positions.
  - If length > `max_wire_length_before_reroute` or crosses too many other wires (`max_crossings_per_segment`):
    - Insert one or more **reroute nodes**:
      - Place reroute on shared axes to create right‑angle paths (e.g. horizontal then vertical).
      - Keep reroutes aligned in rows/columns where possible.
- Ensure reroutes do not obscure nodes or comments; treat reroutes as minimal‑size nodes during packing.

### 3.6 Exec graph refactor suggestions (optional)

**Goal**: Provide **deterministic, rule‑based refactor hints** the agent can choose to apply via tools (e.g. `blueprint_graph_patch`, `blueprint_apply_ir`, `blueprint_add_variable`, etc.).

- For each function or exec cluster:
  - If `node_count > max_nodes_per_function`:
    - Propose breaking into **helper functions** by:
      - Locating subgraphs that:
        - Have a single entry and exit exec edge.
        - Operate mainly on a limited set of variables or parameters.
      - Naming heuristic: use dominant variable or node type (e.g. `HandleDamage`, `SetupInput`, `UpdateUI`).
  - For repeated subgraphs (isomorphic patterns):
    - Suggest factoring into a macro or function.
  - For deep branch nesting (e.g. nesting depth > N):
    - Suggest early returns or separating into multiple CustomEvents/functions.

These suggestions can be encoded as a **machine‑readable plan** (e.g. IR or patch JSON) instead of free text.

---

## 4. Commenting and documentation rules

The formatter should support deterministic decisions about **when** and **how** to add or normalize comments.

- **EventGraph entry comments**:
  - If an Event node has no leading comment, inject one with a short explanation of its role, e.g.:
    - “Handles player movement input.”
    - “Initializes health and UI at BeginPlay.”
  - Source text is derived from:
    - Event node name, bound input action name, or delegate name.
- **Inline comments**:
  - Within large clusters, optionally add small comment boxes above complex constructs:
    - Multi‑branch decision logic.
    - Timelines/state machines.
    - Latent flow chains (delays, async tasks).
- **Naming normalization** (not strictly layout, but format‑adjacent):
  - Enforce or suggest **PascalCase** for functions/macros and descriptive names.
  - Prefix booleans with `b` (e.g. `bIsJumping`), align with existing style guides.

All of these should be controlled by strict flags like `auto_annotate_events`, `normalize_names`, `max_auto_comments_per_graph` to keep behavior deterministic.

---

## 5. Separation of concerns / multi‑Blueprint factoring

The formatter itself should **not** arbitrarily create new Blueprints, but it can:

- Detect **overloaded Blueprints**:
  - High node count + many unrelated responsibilities (e.g. input, UI, saving, AI logic in one BP).
- Emit **structured diagnostics** like:
  - “This Blueprint contains 4 distinct behavior clusters: Movement, Combat, UI Updates, Save/Load. Consider splitting into separate Blueprints or components.”

These diagnostics can feed into higher‑level tools (asset creation, componentization) while the formatter remains a **per‑graph** layout engine.

---

## 6. Inputs & outputs for an actual formatter tool

To plug into `blueprint_format_graph` / `blueprint_apply_ir` / `blueprint_graph_patch` style tools, specify a clear contract.

- **Tool input** (example schema):
  - `blueprint_path`: `/Game/...` object path.
  - `graph_name` or `graph_id`: which graph to format.
  - `prefs`: object with keys like:
    - `flow_direction`
    - `exec_lane_dx`, `exec_lane_dy`, `data_lane_dx`
    - `branch_vertical_gap`
    - `auto_comment_min_nodes`, `comment_style`
    - `max_wire_length_before_reroute`, `max_crossings_per_segment`
    - `max_nodes_per_function`, `max_nodes_per_event_cluster`
    - `preserve_manual_regions`, `preserve_comment_boxes`
- **Tool output**:
  - On success: updated node positions, possibly new reroute/comment nodes, with a summary:
    - `moved_nodes`: count
    - `added_reroutes`: count
    - `added_comments`: count
    - `refactor_suggestions`: optional list of machine‑readable suggestions.
  - On failure: structured error explaining constraint violations (e.g. graph too large for this pass, or invalid graph id).

---

## 7. What we have vs. what we lack

### 7.1 What we already have (in this repo)

- **Blueprint layout engine**: existing Blueprint formatter and layout code under `BlueprintFormat/` that can:
  - Auto‑layout graphs (`LayoutEntireGraph`, strand‑based layout).
  - Work with your `blueprint_format_graph` and `blueprint_apply_ir` tools.
- **Deterministic tool surface**:
  - `blueprint_format_graph` and `blueprint_apply_ir` already accept layout flags like `layout_scope`, `layout_mode`, `wire_knots`.
  - Tool contracts and prompts that distinguish IR vs general graph patching.

### 7.2 What we lack

- A **formal, documented layout policy** capturing:
  - Preferred lane spacing, branch separation, and reroute thresholds.
  - Comment injection rules and naming heuristics.
- A **stable analysis pass** that surfaces graph complexity metrics and semantic clusters as structured data (right now most of this is implicit in how we call the formatter).
- Explicit **refactor suggestion output** (e.g. “this cluster should be a function named `HandleDamage`”), rather than relying on the LLM to infer that from a messy graph.
- Per‑project or per‑team **style profiles** (e.g. “compact graphs” vs “spacious graphs with heavy commenting”).

### 7.3 Steps to close the gap

1. **Specify a canonical profile** for Blueprint formatting:
   - Fix concrete numeric defaults for spacing, branch gaps, reroute thresholds, and comment policies.
   - Document this profile in code + docs so it’s predictable and testable.
2. **Expose a graph analysis API**:
   - Build a reusable analysis layer that, given a graph, returns:
     - Entry nodes, exec clusters, data clusters.
     - Complexity metrics and structural hints.
   - Feed this into both the formatter implementation and LLM context (read‑only).
3. **Align `blueprint_format_graph` implementation with this spec**:
   - Ensure internal layout heuristics match the lane/branch/comment/reroute rules above.
   - Add tests that snapshot layout for representative graphs and assert idempotence.
4. **Add structured refactor suggestions**:
   - Extend formatter to optionally emit a list of “suggested refactors” as JSON:
     - Candidate helper functions, macros, or CustomEvents.
     - Overloaded Blueprints that should be split.
   - Let the agent decide whether to apply them via `blueprint_graph_patch` / IR tools.
5. **Support style presets**:
   - Introduce named presets (e.g. `compact`, `spacious`, `debug-heavy`) that map to different parameter sets, but keep behavior deterministic once chosen.
6. **Document and prompt‑integrate**:
   - Add a short section in prompts describing what the formatter does and when to call it (e.g. after large patches, before presenting graphs to users).
   - Make it clear that manual layout regions (if tagged) will be preserved.

With these steps, the Blueprint formatter evolves from a “black‑box auto layout” into a **predictable, parameterized formatting engine** that aligns with how experienced Unreal developers structure their Blueprints—while remaining fully machine‑driven and testable.

---

## 8. Existing expansion plan (merged from old doc)

This section folds in the previous `blueprint-formatter-expansion` planning notes so we have a single source of truth.

### 8.1 Knots (`UK2Node_Knot`) for prettier wires

- **Current state**:
  - Data‑wire knots are already implemented behind `EUnrealBlueprintWireKnotAggression` (`off | light | aggressive`) and invoked as part of the post‑layout pass.
  - Exec‑wire knots are **not** implemented; we avoid touching exec flow until we have strong invariants.
- **Requirements / next steps**:
  - Tune heuristics using the parameters in §2 (especially `max_wire_length_before_reroute` and `max_crossings_per_segment`) so we only knot truly long/messy wires.
  - Keep knot insertion scoped by **layout scope**:
    - Use knots automatically for `layout_scope: patched_nodes` or selection to avoid surprise on user‑owned regions.
    - Reserve full‑graph, aggressive knot passes for explicit `blueprint_format_graph` / `blueprint_compile(format_graphs=true)` calls.
  - Consider carefully scoped **exec knotting** only where it does not harm readability (e.g. splitting very long, straight exec wires without altering topology).

### 8.2 Multi‑strand layout (stacked horizontal lanes)

- **Current state**:
  - Multi‑strand layout is implemented and used by `LayoutEntireGraph` with deterministic ordering (using `NodeGuid` as a tiebreaker).
- **Design guidance**:
  - Keep a **primary exec spine** (main event flow) in a stable row; place rarer/deeper branches below or to the side so “happy path” remains visually dominant.
  - Use exec‑graph analysis from §3.1/3.2 to:
    - Assign strands for each branch.
    - Stack strands vertically with consistent gaps.
  - Document the chosen heuristics in code (already partially done in `BlueprintGraphStrandLayout` and `BlueprintLayoutPolicy.h`) so prompts can reference them.
- **Implementation steps**:
  - Ensure `EUnrealBlueprintGraphLayoutStrategy::MultiStrand` is the default for AI‑driven formatting.
  - Add tests that lock in expected multi‑strand layouts for a few canonical graphs (simple branch, nested branch, sequence).

### 8.3 Graph comments and settings integration

- **Current state**:
  - `BlueprintCommentsMode` in `UUnrealAiEditorSettings` (`Off | Minimal | Verbose`) controls how aggressively we comment in prompts and tools.
  - IR supports `op: "graph_comment"`; round‑trips via `blueprint_export_ir` / `blueprint_apply_ir`, and `member_node_ids` drive automatic comment fitting.
- **Requirements / next steps**:
  - Ensure formatter respects `BlueprintCommentsMode`:
    - `Off`: do not create new comments; only resize existing boxes.
    - `Minimal`: only top‑level region comments (e.g. per event or major subsystem).
    - `Verbose`: allow inline comments for complex constructs (as in §4).
  - Add tests that:
    - Add nodes under a comment, run formatter, and assert that bounds update correctly.
    - Round‑trip `graph_comment` IR and preserve membership.

### 8.4 Custom events as a refactor tool

- **Goal**:
  - When graphs get wide or show repeated patterns, the agent should be able to introduce `K2Node_CustomEvent` + calls as a **deterministic refactor**, not only when the user explicitly asks.
- **Implementation hooks**:
  - IR and `blueprint_graph_patch` already support creating custom events (`k2_class: K2Node_CustomEvent`, `custom_event` object).
  - Formatter’s **refactor suggestions** (see §3.6) should include:
    - Candidate subgraphs that are good custom‑event extraction targets.
    - Suggested event names (based on variables / node types).
  - The agent can then apply these suggestions via `blueprint_graph_patch` or `blueprint_apply_ir` using the deterministic contracts.

### 8.5 “Format selection” tool surface

- **Current state**:
  - `blueprint_format_selection` exists and runs `LayoutSelectedNodes` on the currently selected nodes in the active Blueprint editor.
  - There is a console alias `UnrealAi.BlueprintFormatSelection` and a best‑effort editor toolbar/menu integration.
- **Requirements / next steps**:
  - Treat **selection formatting** as the default for “small, local fixes” coming from the agent:
    - When a patch touches only a small area, prefer `blueprint_format_selection` targeting that region instead of full‑graph format.
  - Add minimal harness tests that:
    - Select a known subset of nodes.
    - Call `blueprint_format_selection`.
    - Assert that:
      - Selected nodes move into a clean layout.
      - Non‑selected nodes retain their original positions (modulo knot/comment passes if enabled).

### 8.6 Consolidated implementation roadmap

Bringing both documents together, the roadmap is:

1. **Lock down a canonical layout profile** (spacing, multi‑strand defaults, knot aggression, comment policy) and encode it in `BlueprintLayoutPolicy.h` + settings.
2. **Finish wiring the analysis API** and expose its outputs (clusters, metrics) to both formatter and LLM context.
3. **Stabilize knot and multi‑strand behavior** with tests:
   - Pairwise overlap resolution.
   - Knot insertion thresholds.
   - Multi‑strand ordering invariants.
4. **Integrate `BlueprintCommentsMode`** into formatter decisions, ensuring IR `graph_comment` and automatic comments behave consistently with user settings.
5. **Implement structured refactor suggestions**, including:
   - Helper functions/macros.
   - Custom events for repeated or wide subgraphs.
6. **Harden selection formatting** for local changes and ensure the agent’s routing prefers:
   - `layout_scope: patched_nodes` during patches.
   - `blueprint_format_selection` for small regions.
   - `blueprint_format_graph` / `blueprint_compile(format_graphs=true)` only for intentional full‑graph passes.

This keeps everything in a single, up‑to‑date specification while reflecting the concrete implementation already present under `BlueprintFormat/`.

