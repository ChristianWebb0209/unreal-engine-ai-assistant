# Blueprint formatter expansion (planning)

This document tracks how we intend to grow the in-plugin Blueprint layout stack beyond today’s **node-positioning** pass. Implementation lives under `Plugins/UnrealAiEditor/.../Private/BlueprintFormat/` (vendored layout service + bridge).

**Current baseline:** horizontal spacing for touched or whole graphs, plus **comment-box containment** when comments already exist. No **knot** insertion, no multi-row “strand” layout, no IR for **graph comments**, no Blueprint-editor toolbar.

---

## 1. Knots (`UK2Node_Knot`) for prettier wires

**Goal:** Insert reroute / knot nodes so **data** wires (and later **exec** where valid) are shorter, less diagonal, and easier to read—**semantics unchanged**.

**Work items:**

- Post-layout (or integrated) pass: detect **long edges**, **high crossing count**, or **edges that cut through** unrelated node bounds → insert one or more knots, reconnect pins.
- Start with **data pins** only; validate **exec** knot behavior per **UE 5.7** APIs and compiler invariants.
- Optional **aggressiveness** tier (settings or tool arg): `off` | `light` | `aggressive`.
- Keep logic in `BlueprintFormat/` (e.g. dedicated `.cpp` for knotting), callable from existing `FUnrealBlueprintGraphFormatService` or a sibling helper.

**Risks:** Over-knotting looks noisy; must respect **user-owned** graphs (tie to `layout_scope` / explicit “format” tools, not silent full-graph by default).

---

## 2. Multi-strand layout (stacked horizontal “lanes”)

**Problem:** Today’s algorithm is essentially **one horizontal strip** (per pass). Real graphs behave like **several parallel flows** (branches, independent chains). We want **many horizontal strands**, **stacked vertically**, with consistent spacing.

**Convention / what others do:**

- **Sugiyama-style layered layouts** (left-to-right or top-to-bottom) are common in **generic** graph tools; Blueprint editors often **hybrid**: primary flow along one axis, **branches** as **sub-rows** or **columns**.
- **Pure “longest strand bottom vs top”** is **not** a universal standard—different tools pick different heuristics. Common heuristics include:
  - **Primary exec spine** (e.g. from main event or leftmost entry) gets a **stable row**; side branches **above or below** by **BFS/DFS order** or **minimize crossings**.
  - **Semantic grouping**: keep **same subsystem** (movement, damage, UI) in adjacent rows—often **better for Blueprints** than sorting only by strand length.
- **Readable convention for gameplay graphs:** many teams prefer **main flow top-to-bottom** or **left-to-right** with **deeper / rarer** branches **below** or **to the side**, so the “happy path” is seen first. This aligns with **how humans read** more than with **longest strand first**.

**Proposal for us:**

- **Phase A — structural strands:** Partition nodes into **weakly connected components** or **exec chains** (respecting K2 exec edges), layout **each strand** as a horizontal sequence, **stack strands** on the Y axis with a fixed **lane gap**.
- **Phase B — ordering:** Default **semantic-first** ordering when we have signals (e.g. **event → tick → custom event** order, or IR `node_id` / graph order); fall back to **deterministic** tie-break (`NodeGuid`) for stability.
- **Phase C — crossing reduction:** optional pass (may interact well with **knots**).

Document **chosen heuristics** in code comments once implemented so harnesses and prompts can refer to them.

---

## 3. Graph comments (boxes) + user setting + IR + resizing

**Already in place:**

- **User setting:** `UUnrealAiEditorSettings::BlueprintCommentsMode` (`Off` | `Minimal` | `Verbose`) and prompt token `{{BLUEPRINT_COMMENTS_POLICY}}` in `01-identity.md` (via `UnrealAiPromptChunkUtils`).

**Still needed:**

- **IR + materializer:** Add something like `op: graph_comment` (title, optional bounds, optional member `node_id`s) in `blueprint_apply_ir` / `blueprint_export_ir` so the agent can **create** and **round-trip** comment boxes when policy is not `Off`.
- **Prompts + catalog:** Align `04-tool-calling-contract.md` and tool descriptions so the model knows **when** to emit comments per mode (**Minimal** vs **Verbose**).
- **Existing user comments:** When we **insert or move** nodes, **refit** comment rectangles so they still tightly contain their members (we already have **containment / push** logic in the vendored layout; extend it so **adding nodes inside a user comment** grows the box, and **removing / moving out** shrinks or updates membership consistently).
- **Tests:** Automation or unit tests for “node added under comment → bounds update.”

---

## 4. Custom events (agent judgment)

**Goal:** The agent may **choose** to introduce **`UK2Node_CustomEvent`** (and matching `call_custom_event` or equivalent wiring) when it improves **readability** or **reuse**, not only when the user asks.

**Work items:**

- **Prompts:** Clear guidance—when graphs get **wide** or **repeated patterns** appear, prefer **Custom Event** + calls vs one giant chain.
- **IR:** Already supports `custom_event` in the materializer; ensure **export** and **merge_policy** stories are documented so the model doesn’t duplicate events.
- **Optional tool:** “Extract to custom event” (future) for deterministic refactors; until then, **IR-only** creation is enough.

---

## 5. “Format selection” + Blueprint editor UI button

**Backend capability:** `FUnrealBlueprintGraphFormatService::LayoutSelectedNodes` already exists in [`BlueprintGraphFormatService.h`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/BlueprintFormat/BlueprintGraphFormatService.h)—we can wire:

- A **tool** (e.g. extend `blueprint_format_graph` with `format_selection_only` + graph context, or new `blueprint_format_selection`) that reads **current selection** from the active Blueprint editor, **or**
- A direct **editor command** from UI.

**UI button (Blueprint editor toolbar):**

- We **previously** kept the standalone formatter module’s startup **minimal** because **UE 5.7** changed or removed **Blueprint editor toolbar extension** APIs used in older samples—hooking reliably without stale APIs takes **dedicated** investigation (likely `FBlueprintEditorModule`, `UToolMenus`, or version-specific extension points).
- **Plan:** Spike a **small** editor-only registration (one menu entry or toolbar button) that calls `LayoutSelectedNodes` on the **active graph** and **selected nodes**; guard with `#if` / runtime checks if APIs differ by minor version.
- **Fallback:** Document **Editor Utility** or **console command** if toolbar registration remains fragile—still valuable for users and for dogfooding.

---

## 6. Suggested implementation order

1. **Tool + harness path for “format selection”** (no new toolbar until stable).
2. **Knot insertion (data wires, light mode)** + respect `layout_scope` / user graph policy.
3. **Multi-strand stacked layout** for `LayoutEntireGraph` / optional apply mode.
4. **`graph_comment` IR** + prompt/catalog updates tied to **Blueprint comments** setting.
5. **Blueprint editor toolbar / menu** for format selection (API spike).
6. **Custom event** guidance refinement + optional refactor tool later.

---

## References in repo

- Layout implementation: `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/BlueprintFormat/`
- Layout policy notes: `BlueprintLayoutPolicy.h` (user-owned graph defaults)
- Agent comment setting: `UUnrealAiEditorSettings` + `UnrealAiPromptChunkUtils::ApplyTemplateTokens`
