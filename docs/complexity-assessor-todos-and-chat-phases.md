# Planning loop: complexity assessor, todo plans, continuations

**Status:** design spec for v1 — aligns implementation and prompts.  
**Related:** [`context-service.md`](context-service.md), [`agent-harness.md`](agent-harness.md), [`agent-and-tool-requirements.md`](../agent-and-tool-requirements.md).

---

## 1. Why this exists (v1 positioning)

**Goal:** Be **meaningfully better than typical editor copilots on large, ambiguous tasks.** Most competitors still **one-shot** or lightly tool-loop without a **durable plan artifact**—so they skip dependencies, forget acceptance criteria, and look “done” before work is finished.

**Our v1 bet:** A **planning structure** (complexity signal + validated todo plan + harness-driven execution) is the fastest way to win on **advanced tasks** without waiting on embeddings or a product backend. Retrieval can come later; **orchestration discipline** is the differentiator.

---

## 2. What we ship (short)

| Piece | Role |
|-------|------|
| **`FUnrealAiComplexityAssessor`** | Cheap, deterministic **score + signals** every turn (feeds context). **Implemented** in [`UnrealAiComplexityAssessor.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Planning/UnrealAiComplexityAssessor.cpp); consumed by [`FUnrealAiContextService::BuildContextWindow`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/FUnrealAiContextService.cpp) and prompt [`{{COMPLEXITY_BLOCK}}`](../../Plugins/UnrealAiEditor/prompts/chunks/03-complexity-and-todo-plan.md). |
| **Structured todo plan** | Versioned **`unreal_ai.todo_plan`** — validated JSON, **not** prose-only plans. |
| **Harness continuation** | After a valid plan, the run **does not end** for the user: sub-turns execute steps (within **hard limits**) until done, pause, or cancel. |
| **Chat UI phases** | One user message can produce **multiple visible phases** (plan panel → “Step 2/5” → tools). No fake “final answer” while work continues. |

**Non-goals (this feature):** perfect complexity prediction, occlusion reasoning, cross-thread plan libraries.

---

## 3. `FUnrealAiComplexityAssessor`

**Location:** e.g. `Private/Planning/` — feeds **`BuildContextWindow`**, does **not** replace the context service.

**Inputs (deterministic):** user message stats (length, bullets), attachment/mention count, `EUnrealAiAgentMode`, light use of `editorSnapshot` (e.g. many selections).

**Output:**

```text
ScoreNormalized   // 0..1 (single convention for the whole product)
Label               // low | medium | high
Signals[]           // short strings for logs / optional UI
bRecommendPlanGate // true when ScoreNormalized >= tunable threshold
```

**Why code + score?** Model-only “is this big?” **drifts** with prompt edits. Fixed thresholds give **stable** behavior and tuning without rewriting prompts weekly.

---

## 4. Context block (every request)

Always append something like:

```text
[Complexity]
score: 0.72 (high)
signals: long_message; bullets; agent_mode
policy: if high OR clearly multi-goal, prefer emitting unreal_ai.todo_plan before destructive tools
```

Instructions (paraphrase): when policy applies, **emit the structured plan first**; do not jump straight into risky edits.

---

## 5. Plan transport — **tool-first (v1 default)**

| Approach | v1 |
|----------|-----|
| **Primary:** tool call e.g. **`agent_emit_todo_plan`** | Harness records the JSON **once**, validates against schema, persists. **Preferred.** |
| **Fallback:** fenced JSON in assistant text | Only if tool path unavailable; one repair pass max, then fail visibly. |

Avoid **`response_format` JSON-only** as the main path if it conflicts with tools.

---

## 6. Schema: `unreal_ai.todo_plan` v1

**Check in:** `docs/schemas/unreal_ai_todo_plan.v1.schema.json` (authoritative).

Illustrative shape:

```json
{
  "schemaVersion": 1,
  "kind": "unreal_ai.todo_plan",
  "title": "…",
  "summary": "One short paragraph for humans.",
  "definitionOfDone": ["…"],
  "assumptions": ["…"],
  "risks": ["…"],
  "steps": [
    {
      "id": "step_1",
      "title": "…",
      "detail": "…",
      "dependsOn": [],
      "suggestedTools": ["editor_get_selection"],
      "status": "pending"
    }
  ],
  "nextAction": "execute_step",
  "cursorStepId": "step_1"
}
```

**Caps:** max steps (e.g. 12) enforced in harness; truncate with a warning in context.

---

## 7. Summary + pointer (critical for context cost)

**Problem:** Re-pasting the **full plan JSON** on every execution sub-turn **wastes tokens** and invites drift.

**Rule:** After the plan is stored (e.g. `context.json` → `activeTodoPlan`):

- **Execution sub-turns** send the model:
  - A **short summary** (1–3 sentences) + **current step title**
  - A **pointer**: `planId` / thread-local id, **`cursorStepId`**, and **`step_ids_done`**
  - **Not** the full step list each time unless a **repair** or **user edit** forces it

The harness (or context formatter) **hydrates** full step text from disk when building the API request, so the **LLM prompt stays lean** while the **canonical plan** stays complete on disk.

**Optional:** One **compact** “step index” line in the prompt (titles only) for orientation—still far smaller than full JSON.

---

## 8. Harness: continuation + **hard rails**

Orchestration lives in the harness (or small `FUnrealAiPlanOrchestrator` helper). States: `Idle` → `AwaitingPlan` / `ExecutingPlan` → `PausedForUser` → `Done` / `Failed`.

**Automatic sub-turns must be bounded:**

| Rail | Purpose |
|------|--------|
| **Max sub-turns** per user message | Stops runaway loops (model/tool ping-pong). |
| **Max wall-clock time** (optional) | Safety for long sessions. |
| **Cancel** | Always clears in-flight continuation; define whether partial plan stays on disk. |

**User setting (v1):** **`Auto-continue after plan`** — on = harness runs execution sub-turns automatically within rails; off = show plan and require explicit **Continue** (still one product, two behaviors).

---

## 9. UI: phases, not fake “done”

- **Terminal for the user** only when the harness emits **run complete** for that **user turn** (not when the first assistant chunk ends).
- **Phases:** plan widget, “Continuing — step *k*”, tool rows, sub-turn badge (**Round 2/5**), etc. Share **`TurnSessionId`** + **`PhaseIndex`** on sink events ([`IAgentRunSink`](agent-harness.md)).
- **“Thinking” / reasoning:** show **real** provider reasoning / extended thinking if the API exposes it; otherwise use **neutral** copy (“Continuing…”, “Running tools…”) — **no fabricated chain-of-thought**.

**Same pattern** applies later to clarifications, approvals, and Level-B workers: **one continuation model** in the message list.

---

## 10. Persistence

| What | Where |
|------|--------|
| Canonical plan + step status | `context.json` (`activeTodoPlan` or equivalent) — bump `schemaVersion` |
| API history | `conversation.json` — sub-turn assistant messages as today |
| Optional debug | `run_meta` fields or logs: continuation count, cancel reason |

---

## 11. Mode notes

- **Ask:** plans OK; **no** mutating tool auto-loop.
- **Fast / Agent:** full loop; **Agent** is the default for “big” work; complexity block still appears so the model **sees** pressure to plan.

---

## 12. Rollout (minimal path)

1. Assessor + context block + **tool `agent_emit_todo_plan`** + schema validate + persist.
2. **Summary + pointer** execution sub-turns + rails + sink phases.
3. Slate todo panel + **Auto-continue** setting.
4. Clarify / approval hooks reuse the same phase IDs.

---

## Document history

| Date | Change |
|------|--------|
| 2026-03-20 | Initial brainstorm |
| 2026-03-20 | v1 positioning, tool-first, summary+pointer, rails, honest UI, simplified |
