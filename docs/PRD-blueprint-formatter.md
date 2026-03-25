# PRD: Blueprint formatter & graph merge service

**Status:** Active — layout (formatter plugin) + merge v1 (`merge_policy`, `event_tick`, exec-tail walk) implemented in Unreal AI Editor; see §5–6.  
**Owner:** Unreal AI Editor plugin  
**Related:** [`AGENT_HARNESS_HANDOFF.md`](AGENT_HARNESS_HANDOFF.md), `UnrealAiToolDispatch_BlueprintTools.cpp` (`blueprint_export_ir`, `blueprint_apply_ir`)

---

## 1. Summary

Build a **dedicated Blueprint formatter service** that:

1. **Lays out** nodes on a Blueprint graph so AI-generated edits are readable (not stacked at the origin, sensible flow).
2. **Understands existing graph structure** when the AI applies changes, so it can **attach new logic to the end of an existing execution chain** (e.g. **Event Tick**) instead of creating a **second** Tick event or duplicate entry points.

The formatter is **layout**; **merge/anchor resolution** is a separate concern (same service boundary, distinct pipeline stage) so we can test and evolve them independently.

---

## 2. Problem statement

### 2.1 Layout

Today, IR apply may place all nodes at `(0,0)` or use a naive horizontal strip; complex graphs are unreadable and hard to debug.

### 2.2 Duplicate events & disconnected edits

The model often emits a full mini-graph (e.g. `event_tick` + `call_function`) **from scratch**. If the asset already has **Event Tick** with logic, a second apply creates:

- **Duplicate Tick nodes** (invalid or confusing duplicate entry points), or
- **Disconnected islands** of nodes that never run.

The AI needs **explicit policy and data** to:

- **Reuse** the existing Tick (or BeginPlay, BeginOverlap, etc.) when the user intent is “add to what’s there.”
- **Only create** a new event node when the intent is “new entry point” or the graph is empty.

---

## 3. Goals

| ID | Goal |
|----|------|
| G1 | **Formatter service** is a named module with a small public API (no UI coupling). |
| G2 | **Layout** runs after graph mutations from AI tools (or on demand), with deterministic, testable behavior. |
| G3 | **Merge/anchor** step can resolve “attach after existing Tick” using **engine-accurate** graph introspection (not guesswork from JSON alone). |
| G4 | Behavior is **configurable** via IR flags / tool args (e.g. `merge_policy`, `anchor`), with safe defaults. |
| G5 | **Non-destructive** to user hand-layout when configured (e.g. “layout only new nodes” or “layout only AI-touched subgraph”). |

---

## 4. Non-goals (v1)

- Replacing the Blueprint editor’s built-in “Align” / “Straighten” commands or pixel-perfect parity with the editor’s auto-arrange.
- Full-graph layout for **every** Blueprint compile in the project (only AI-driven or explicitly invoked paths).
- Perfect handling of **all** K2 node types (macros, latent flows, composite nodes) in v1; start with common exec + data edges.

---

## 5. Architecture

### 5.1 Service boundary

**Implemented (layout v1):** standalone plugin **`Plugins/UnrealBlueprintFormatter`** — public API **`FUnrealBlueprintGraphFormatService`** (`BlueprintGraphFormatService.h`): exec-depth layout, `LayoutAfterAiIrApply` for Unreal AI IR (all `x,y` at zero). Editor: **Format Graph** on `AssetEditor.BlueprintEditor.ToolBar`. **Merge / event-anchor** stages remain future work (can live in the same plugin).

Previously proposed name (superseded for layout-only milestone):

`Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/Blueprint/` — not used; formatter is its own plugin.

**Responsibilities (single service, staged pipeline):**

| Stage | Name | Input | Output |
|-------|------|--------|--------|
| A | **Introspect** | `UBlueprint*`, `UEdGraph*`, optional scope | `FUnrealAiBlueprintGraphModel` — nodes, pins, exec topology, **event anchors** |
| B | **Resolve merge** | `FUnrealAiBlueprintGraphModel` + **merge policy** + IR patch | **Anchors** (which existing node/pin to extend), **IDs** for reuse vs create |
| C | **Layout** | `UEdGraph*` + subset of nodes to position + layout options | Updated `NodePosX` / `NodePosY` |

Stages **A–C** may be called from:

- `blueprint_apply_ir` (merge planning before node creation for builtin events; link phase uses tail remap).
- `blueprint_format_graph` tool (full-graph layout; optional).

**Dependency:** Formatter uses only `UObject` / graph APIs (`UEdGraph`, `UK2Node_Event`, `UEdGraphSchema_K2`, etc.), not Slate.

### 5.2 Relationship to existing tools

- **`blueprint_export_ir`** remains the **lossy** JSON export for the model; the service may use **internal richer structures** (same raw data, more cached topology).
- **`blueprint_apply_ir`** becomes the **orchestrator**: parse IR → optional **merge** → create/link → **layout** → compile.

---

## 6. Merge policy: how we avoid a second Tick

### 6.1 Concepts

- **Event anchor:** A `UK2Node_Event` (or custom event) that matches a **stable signature**: e.g. `(member_name, outer_class)` for `ReceiveTick` on the Blueprint’s generated class.
- **Exec tail:** A terminal **exec output** pin on the “main” chain from that event (e.g. last node in the white exec line), or the **unconnected** exec output of the event if the chain is empty.

### 6.2 Policies (tool / IR)

| Policy | Behavior |
|--------|----------|
| `create_new` | Always create IR nodes as declared (current behavior); may duplicate events. |
| `append_to_existing` | For each `op` that is an **event** (`event_begin_play`, `event_tick`, …), **if** a matching event anchor **already exists** in the target graph, **do not create** a new event node; **connect** the new IR subgraph to the **exec tail** of the existing anchor (or merge into a designated `custom_event`). |
| `replace_graph` | (Future) Clear or replace a named region; out of scope for v1 unless needed. |

**Default:** `append_to_existing` for **Ubergraph** edits when `graph_name` is the event graph; `create_new` when explicit flag or empty graph.

### 6.3 Algorithm sketch (`append_to_existing`)

1. **Load graph** and enumerate `UK2Node_Event` nodes.
2. For IR node with `op == event_tick` (or equivalent):
   - Resolve `UClass` for the Blueprint (generated class, not `AActor` unless correct for this BP).
   - Find existing `ReceiveTick` node **for this BP’s class**.
3. If found:
   - **Skip** creating a new `event_tick` from IR.
   - Map IR `node_id` for that event to the **existing** `UEdGraphNode*` (GUID in `NodeById` map).
   - Find **exec tail**: walk from event’s `then` pin along exec links until no further exec output; or use last node in BFS order along exec-only edges.
   - **IR links** that would start from `event_tick.then` → instead start from **tail’s** appropriate output pin (often `then`).
4. If not found:
   - **Create** a new event node as today.

### 6.4 Pin naming & robustness

- Reuse existing **`NormalizeBlueprintIrPinToken`** / `FindPinByName` behavior for links.
- Document **canonical K2 pin names** in tool catalog (`execute`, `then`, …).

### 6.5 Edge cases

- **Multiple Tick nodes** (user error or corruption): **v1** — pick one deterministically (e.g. lowest `NodePosY` then `NodePosX`) and log a warning in tool result JSON.
- **Latent nodes** (Delay, etc.): tail may be **Completed** pin; v1 can treat “last non-latent exec” or require IR to specify `anchor_pin` later.

---

## 7. Layout (formatter)

### 7.1 Inputs

- Target `UEdGraph*`.
- Set of `UEdGraphNode*` to layout (or “all nodes”).
- Options: `horizontal_gap`, `vertical_gap`, `layering_strategy` (`exec_depth` first).

### 7.2 v1 algorithm

- **Exec layering:** Build a DAG from **exec pins** (`UEdGraphSchema_K2::IsExecPin`). Assign layers by depth from selected roots (event outputs).
- **Position:** Layer → column `X`; within layer, order nodes by stable sort (GUID), assign `Y` increments.
- **Data-only nodes:** Attach to nearest layer of their driving exec node (or place in a side column).

### 7.3 When to run

- After `blueprint_apply_ir` succeeds, if `layout: true` (default **true**) or if all positions were default/zero (see existing `MaybeAutoSpaceIrNodes` heuristic — **replace** with service call).

---

## 8. API surface (proposal)

```cpp
// Pseudocode — actual names in implementation.

struct FUnrealAiBlueprintFormatterRequest
{
    UBlueprint* Blueprint = nullptr;
    UEdGraph* Graph = nullptr;
    EUnrealAiBlueprintMergePolicy MergePolicy = EUnrealAiBlueprintMergePolicy::AppendToExisting;
    // Optional: explicit anchor override for advanced IR.
};

struct FUnrealAiBlueprintFormatterResult
{
    bool bOk = false;
    FString ErrorMessage;
    TArray<FString> Warnings; // e.g. duplicate Tick nodes
};

class FUnrealAiBlueprintGraphService
{
public:
    // Pre-apply: rewrite IR or return mapping from IR node_id -> existing node (if merge).
    // Post-apply: layout nodes in graph.
    static FUnrealAiBlueprintFormatterResult LayoutGraph(UEdGraph* Graph, const FUnrealAiLayoutOptions& Options);
    static bool TryResolveEventAnchor(/* ... */, FUnrealAiBlueprintEventAnchor& OutAnchor);
};
```

Exact signatures will align with `FBlueprintIr`/`TryParseIr` flow.

---

## 9. Observability

- Tool JSON: `layout_applied`, `merge_policy_used`, `anchors_reused[]`, `warnings[]`.
- Automation tests: small graphs with pre-placed Tick + apply IR that adds a Print node **without** a new Tick node.

---

## 10. Open questions

1. Should **user** choose merge policy per message, or only **system** default in settings?
2. **Function graphs** (not EventGraph): merge policy differs — `append_to_existing` may mean “append to function entry” — define separately.
3. **AnimBlueprint** / state machines: exclude from v1 or separate PRD.

---

## 11. Task list

### Phase 0 — Design lock

- [ ] **T0.1** Confirm service name and folder (`Private/Tools/Blueprint/`).
- [ ] **T0.2** Add merge policy enum to IR schema + `UnrealAiToolCatalog.json` + prompt chunk `04-tool-calling-contract.md`.

### Phase 1 — Introspection & anchors

- [ ] **T1.1** Implement `FUnrealAiBlueprintGraphIntrospection` (or nested in service): list event nodes with `EventReference` + resolved `UFunction*`.
- [ ] **T1.2** Implement **exec tail** resolution for a given `UK2Node_Event` (walk exec edges; stop conditions for latent nodes).
- [ ] **T1.3** Unit tests: empty graph, single Tick, Tick + Print chain, duplicate Tick (warning path).

### Phase 2 — Merge in `blueprint_apply_ir`

- [ ] **T2.1** Parse `merge_policy` / `layout` from IR root (defaults).
- [ ] **T2.2** When `append_to_existing` and IR declares `event_tick` / `event_begin_play`, resolve or create; **populate `NodeById`** with existing node when skipping creation.
- [ ] **T2.3** Remove or bypass duplicate event node creation in `CreateNodeFromDecl` when mapped to existing node.
- [ ] **T2.4** Link phase: ensure links referencing skipped IR ids still resolve.

### Phase 3 — Layout service

- [ ] **T3.1** Move/replace `MaybeAutoSpaceIrNodes` with `FUnrealAiBlueprintGraphService::LayoutGraph`.
- [ ] **T3.2** Implement exec-layer layout (v1); optional “layout only nodes touched” flag.
- [ ] **T3.3** Call layout at end of `blueprint_apply_ir` under transaction + `Modify()` rules.

### Phase 4 — Docs & tooling

- [ ] **T4.1** Update this PRD status + link from `README.md` or `docs/tool-registry.md`.
- [ ] **T4.2** Add `blueprint_compile` to acceptance checklist in agent prompts when merge/layout used.

---

## 12. Revision history

| Date | Author | Notes |
|------|--------|--------|
| 2025-03-24 | — | Initial PRD + tasks |
