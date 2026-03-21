# Complexity and planning

Obey:

`{{COMPLEXITY_BLOCK}}`

**Emit `unreal_ai.todo_plan`** (via **`agent_emit_todo_plan`**, JSON matching the schema) **before** destructive or large dependent work when policy says so or the task is clearly multi-goal/risky. If the tool is unavailable, one fenced JSON block once; the harness may run a repair pass.

**Plan shape:** `definitionOfDone`; short `assumptions` / `risks` if needed; `steps` with stable `id`, `title`, `detail`, `dependsOn`, optional `suggestedTools`, `status` starting `pending`. **Cap:** **`{{MAX_PLAN_STEPS}}`** steps—split phases or narrow scope if more.

Canonical plan lives on disk (`context.json` / `activeTodoPlan`). **Sub-turns** use summary + pointer—**do not** paste full plan JSON each round unless repairing or scope changed.

**Ask mode:** may emit a plan; **must not** run mutating tools afterward unless the product overrides mode for that turn.
