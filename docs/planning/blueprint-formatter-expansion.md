# Blueprint formatter expansion (planning)

This document tracks how we intend to grow the in-plugin Blueprint layout stack beyond today’s **node-positioning** pass. Implementation lives under `Plugins/UnrealAiEditor/.../Private/BlueprintFormat/` (vendored layout service + bridge).

**Current baseline:** exec-linked graph layout for touched/whole graphs, with optional **data-wire knot** insertion and **multi-strand** lane layout; supports **`graph_comment` IR nodes** and post-layout comment reflow; and includes a “format selection” surface via the `blueprint_format_selection` tool + `UnrealAi.BlueprintFormatSelection` console command (best-effort Blueprint editor toolbar integration).

---

## 1. Knots (`UK2Node_Knot`) for prettier wires

**Status:** Implemented **data-wire** knots using `UK2Node_Knot` (semantics unchanged) with an aggressiveness tier (`off` | `light` | `aggressive`). Exec-knotting is not implemented yet.

**Work items (remaining / follow-ups):**

- Post-layout / integrated edge heuristics (tuning: long edges / crossings / cut-through bounds).
- Optional follow-up: include **exec** knotting only where UE 5.7 invariants allow safe reroute.
- Keep knot logic in `Private/BlueprintFormat/` behind format-service options (already wired for the data-wire pass).

**Risks:** Over-knotting looks noisy; must respect **user-owned** graphs (tie to `layout_scope` / explicit “format” tools, not silent full-graph by default).

---

## 2. Multi-strand layout (stacked horizontal “lanes”)

**Status:** Implemented multi-strand layout as stacked horizontal lanes (deterministic topology + `NodeGuid` tie-break), giving Blueprint “rows” with consistent lane gaps.

**Convention / what others do:**

- **Sugiyama-style layered layouts** (left-to-right or top-to-bottom) are common in **generic** graph tools; Blueprint editors often **hybrid**: primary flow along one axis, **branches** as **sub-rows** or **columns**.
- **Pure “longest strand bottom vs top”** is **not** a universal standard—different tools pick different heuristics. Common heuristics include:
  - **Primary exec spine** (e.g. from main event or leftmost entry) gets a **stable row**; side branches **above or below** by **BFS/DFS order** or **minimize crossings**.
  - **Semantic grouping**: keep **same subsystem** (movement, damage, UI) in adjacent rows—often **better for Blueprints** than sorting only by strand length.
- **Readable convention for gameplay graphs:** many teams prefer **main flow top-to-bottom** or **left-to-right** with **deeper / rarer** branches **below** or **to the side**, so the “happy path” is seen first. This aligns with **how humans read** more than with **longest strand first**.

**Implementation approach (now in place, with follow-ups):**

- Phase A — structural lanes: build lane/strand groups (exec-aware), lay out each strand as a horizontal sequence, then stack vertically.
- Phase B — ordering: deterministic lane ordering with `NodeGuid` tie-break for stability.
- Phase C — crossing reduction: may benefit from interaction tuning with knots (remaining: heuristic refinement + tests).

Document **chosen heuristics** in code comments once implemented so harnesses and prompts can refer to them.

---

## 3. Graph comments (boxes) + user setting + IR + resizing

**Already in place:**

- **User setting:** `UUnrealAiEditorSettings::BlueprintCommentsMode` (`Off` | `Minimal` | `Verbose`) and prompt token `{{BLUEPRINT_COMMENTS_POLICY}}` in `01-identity.md` (via `UnrealAiPromptChunkUtils`).
- **IR + materializer:** `op: "graph_comment"` nodes now round-trip via `blueprint_export_ir` / `blueprint_apply_ir` and are refit after layout using `member_node_ids`.

**Still needed (remaining):**

- **Prompts + catalog:** verify Minimal vs Verbose comment policy mapping (mostly done in the canonical prompt chunk + tool catalog).
- **Tests:** automation/unit tests for “node added under comment → bounds update” and membership extraction edge cases.

---

## 4. Custom events (agent judgment)

**Goal:** The agent may **choose** to introduce **`UK2Node_CustomEvent`** (and matching `call_custom_event` or equivalent wiring) when it improves **readability** or **reuse**, not only when the user asks.

**Work items:**

- **Prompts:** Clear guidance—when graphs get **wide** or **repeated patterns** appear, prefer **Custom Event** + calls vs one giant chain.
- **IR:** Already supports `custom_event` in the materializer; ensure **export** and **merge_policy** stories are documented so the model doesn’t duplicate events.
- **Optional tool:** “Extract to custom event” (future) for deterministic refactors; until then, **IR-only** creation is enough.

---

## 5. “Format selection” + Blueprint editor UI button

**Implemented tool surface + UI button:**
- Tool: `blueprint_format_selection` (requires nodes selected in the active Blueprint editor) which runs `LayoutSelectedNodes`.
- Console command: `UnrealAi.BlueprintFormatSelection` (reliable fallback).
- Best-effort Blueprint editor toolbar/menu extension is included (version-specific editor extension APIs can still be fragile across UE 5.7 minors).

---

## 6. Suggested implementation order

1. **Tool + harness path for “format selection”** (tool + console implemented; toolbar best-effort).
2. **Knot insertion (data wires)** + respect `layout_scope` / user graph policy.
3. **Multi-strand stacked layout** for `LayoutEntireGraph` / optional apply mode.
4. **`graph_comment` IR** + prompt/catalog updates tied to **Blueprint comments** setting.
5. **Blueprint editor toolbar / menu** for format selection (remaining: verify reliability in your UE 5.7 build).
6. **Custom event** guidance refinement (mostly prompts; remaining: further catalog/prompt consistency checks).

---

## References in repo

- Layout implementation: `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/BlueprintFormat/`
- Layout policy notes: `BlueprintLayoutPolicy.h` (user-owned graph defaults)
- Agent comment setting: `UUnrealAiEditorSettings` + `UnrealAiPromptChunkUtils::ApplyTemplateTokens`
