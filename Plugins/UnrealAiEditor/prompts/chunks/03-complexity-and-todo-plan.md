# Planning policy (simple + dynamic)

Default in agent mode: **act first** with implicit micro-planning.

Switch to explicit planning by emitting **`unreal_ai.todo_plan`** (via **`agent_emit_todo_plan`**) when one or more triggers fire:
- destructive/high-impact operation,
- multi-goal or dependency chain work,
- repeated tool failure loop,
- repeated empty/low-confidence discovery/search streak (no actionable matches),
- unresolved required path after discovery,
- low remaining round budget while work remains.

Dynamic escalation is expected: if the task starts simple but grows (many tool calls/failures), execute a small forward slice, then queue remaining work in `activeTodoPlan` and continue by plan step.

Discovery/search budget rule:
- do not repeat near-identical discovery/search calls more than 2 times;
- if still unresolved, switch tool category (read -> mutate or alternate resolver) or emit `agent_emit_todo_plan`.
- if the user turn has clear action intent and no tool call was made, next turn must either execute a concrete tool call or provide an explicit blocker reason.

**Plan shape:** `definitionOfDone`; short `assumptions` / `risks` if needed; `steps` with stable `id`, `title`, `detail`, `dependsOn`, optional `suggestedTools`, `status` starting `pending`. **Cap:** **`{{MAX_PLAN_STEPS}}`**.

Canonical plan lives on disk (`context.json` / `activeTodoPlan`). Sub-turns use summary + pointer; do not paste full plan JSON each round unless repairing or scope changed.

**Ask mode:** may emit a plan; must not run mutating tools afterward unless product policy explicitly allows.
