# Learnings: debugging Unreal AI Editor + Blueprint graph patches

This document compiles practical lessons from debugging **streaming tool calls**, **`blueprint_graph_patch` / `blueprint_apply_ir` failures**, and **post-patch graph layout** in this repo. It is meant for future agents and humans triaging similar issues.

---

## 1. `stream_tool_call_incomplete_timeout` (harness, not the model “thinking slowly”)

**Symptoms:** Log spam like `age_events` in the hundreds or thousands while `age_ms` is only a few hundred; then `stream_tool_ready` finally fires, or the call times out.

**Cause:** The harness accumulates `unreal_ai_dispatch` **arguments JSON** from **many small SSE chunks** (e.g. one delta per token). Two limits interact badly:

1. **`StreamToolIncompleteMaxEvents`** — if too low (~128), a **valid** large JSON string hits the **event budget** before the stream finishes, so the tool is treated as stuck.
2. **`MaxToolArgumentsJsonDeserializeChars`** — while the accumulated string exceeds this cap, **`TryParseArgumentsJsonComplete`** stays false, so the call never becomes “ready” and may sit until the event timeout fires.
3. **Low retry budget** for incomplete stream finishes can burn the turn on huge payloads.

**Mitigations in this repo:** Raised defaults in `UnrealAiWaitTimePolicy.h` (e.g. very large event cap, multi‑MiB argument cap), increased stream incomplete retries in `FUnrealAiAgentHarness.cpp`, explicit fail message when arguments exceed max size (mentions splitting / `ops_json_path`).

**Still outside the plugin:** Provider **max output tokens** per completion — raising UE limits does not remove that. For **massive** `ops[]`, use **`ops_json_path`** (UTF‑8 JSON file under `Saved/` or `harness_step/`) so the streamed tool call stays small.

---

## 2. `blueprint_graph_patch` fails with `patch_errors` (atomic rollback)

**Behavior:** On failure, **nothing** from that call persists; `applied_partial` is empty; transaction is cancelled.

**Common agent / payload mistakes**

| Mistake | What to do |
|--------|------------|
| **`K2Node_Branch`** or wrong `k2_class` | Branching is **`/Script/BlueprintGraph.K2Node_IfThenElse`**. Integer math is **`K2Node_CallFunction`** + **`/Script/Engine.KismetMathLibrary`** + e.g. `Less_IntInt`, `Add_IntInt` — not a “math node class” path. |
| **`connect` to `var:JumpCount` or bare variable names** | **`connect`** needs **`NodeRef.pin`**: `patch_id.pin`, **`guid:UUID.pin`**, or export **`node_guid.pin`**. There is no `var:` scheme. |
| **VariableSet “`.Set`” pin** | Value pin is usually **named like the variable** (e.g. **`JumpCount`**). Aliases **`Set` / `Value` / `Input`** are repaired in **`FindPin`** when possible; prefer introspect / **`blueprint_graph_list_pins`**. |
| **`OnLanded` / built-in events** | **`K2Node_Event`** needs **`event_override`**: **`function_name`** (e.g. **`Landed`**) + **`outer_class_path`** (e.g. **`/Script/Engine.Character`** for a Character BP). |
| **`set_pin_default` on wrong target** | Target must be **`patch_id.pin`** or **`guid:….pin`**, not a free variable name. |
| **`add_variable` shape** | Needs **`name`** (or **`variable_name`**) and **`type`** (string like **`int`** or supported object shape). Resolver repair copies **`variable_name` → `name`**. Truncated JSON (e.g. mid-key) is a **stream/model** issue, not fixable in C++. |
| **Nonsense exec wiring** | e.g. branch **`false`** → same branch **`then`**. Exec aliases **`execute` / `Then` / `Else`** are normalized; logic still must make sense. |
| **`blueprint_apply_ir` schema validation** | **`variables[].variable_type`** must match catalog (often a **string** like `"int"`), not arbitrary nested objects. |

**Debugging workflow**

1. Read **`errors[]`** and **`error_codes[]`** and **`suggested_correct_call`** in the tool result JSON.
2. Use **`blueprint_graph_introspect`** and **`blueprint_export_ir`** for real **`node_guid`** and pin names (Enhanced Input: **`Started`**, **`Triggered`**, etc.).
3. For uncertain pins on a specific node, **`blueprint_graph_list_pins`**.
4. For **inserting on an existing wire**, prefer **`splice_on_link`** when it matches the case instead of guessing coordinates.

---

## 3. Post-patch layout: “nodes at origin”, “far right”, “far left”, “not between”

**Context:** With **`auto_layout: true`** and **`layout_scope: patched_nodes`** (default), only **new** patch/IR nodes are run through the layered strip layout. Existing nodes keep their positions unless a dedicated pass pushes them.

**Why new nodes used to appear at X ≈ 0**  
The layered layout treats **event / function entry** nodes as strip roots. If the new subgraph has **no** such node inside the materialized set (common when wiring from **Enhanced Input** outside the set), the algorithm treats the subgraph as disconnected and packs it near the **origin**.

**Neighbor translation (first fix)**  
**`TranslateMaterializedClusterTowardGraphNeighbors`** shifts the new cluster toward **exec predecessors** (or centroid / guarded successor), then **pushes** exec successors (and a horizontal band) **right** when the patch would overlap **`MinSuccLeft`**.

**Bug that caused “everything jumped far LEFT”**  
Using **`DX = (MinSuccLeft - Gap) - BMaxX`** whenever any exec successor existed, **without** checking geometry. If the laid-out cluster’s **`BMaxX`** was already **to the right of** the successor’s X (wide strip, odd links, backward refs), **`DX` became a large negative** and slammed the cluster to the **far left**.

**Fix:** Only use that successor-based **`DX`** when **`MinSuccLeft > BMaxX + 32`**. Otherwise fall back to **centroid** anchoring.

**Hinted `x` / `y` vs strip layout**  
If **`create_node`** uses **non-zero** hints (from introspect), **re-applying full translation** overwrote intentional placement. **Fix:** When hints are applied (**non-all-zero** hint path), run **overlap push only** (`bRepositionCluster = false`); **strip layout** path keeps **full** translation + push.

**Comments**  
Pushed nodes may need comment reflow; **`RunPostLayoutPasses`** + editor formatting settings control reflow / auto-comments.

---

## 4. Quick reference: files touched for these behaviors

| Area | Location |
|------|-----------|
| Stream incomplete limits / argument size | `Plugins/UnrealAiEditor/.../Misc/UnrealAiWaitTimePolicy.h`, `FUnrealAiAgentHarness.cpp` |
| Large inline ops escape hatch | `UnrealAiToolDispatch_BlueprintGraphPatch.cpp` (`ops_json_path`), `UnrealAiToolResolver.cpp`, `UnrealAiToolCatalog.json` |
| Pin aliases (VariableGet/Set) | `UnrealAiToolDispatch_BlueprintGraphPatch.cpp` (`FindPin`) |
| `add_variable` name repair | `UnrealAiToolDispatch_ArgRepair.cpp` |
| Layout translation + push | `BlueprintGraphFormatService.cpp` (`LayoutAfterAiIrApply` → `TranslateMaterializedClusterTowardGraphNeighbors`) |
| Agent-facing contracts | `Plugins/UnrealAiEditor/prompts/chunks/blueprint-builder/07-graph-patch-canonical.md`, `.../04-tool-calling-contract.md` |

---

## 5. Success criteria when verifying a fix

- Large streamed **`unreal_ai_dispatch`** JSON does not false-trigger **`stream_tool_call_incomplete_timeout`** while JSON is still growing validly.
- Failed **`blueprint_graph_patch`** leaves the asset **unchanged** and returns actionable **`errors[]` / `suggested_correct_call`**.
- New nodes land **near wired predecessors**; **successors** shift **right** when there would be overlap; **explicit `x`/`y`** are not overwritten on the hint path.
- No **large negative X** snap from successor-only anchoring when the strip is already past the successor.

---

*Last updated from internal debugging threads on triple-jump / first-person Blueprint edits, streaming timeouts, and graph layout.*
