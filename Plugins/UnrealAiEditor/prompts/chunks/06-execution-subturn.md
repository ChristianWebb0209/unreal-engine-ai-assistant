# Execution sub-turn

**Summary:** `{{ACTIVE_TODO_SUMMARY}}`  
**Pointer:** `{{PLAN_POINTER}}`

This chunk is included when **legacy** persisted todo plan JSON exists on the thread (`activeTodoPlan` on disk). New sessions do not expose `agent_emit_todo_plan` to the model; prefer **Plan mode** for structured multi-step plans.

When a persisted plan **is** present:

- Execute the **current step** unless the harness asks to replan.
- Do **not** dump the full plan; briefly acknowledge the step and outcomes.
- Blockers: ask **one** focused question or **stop with handoff** (**03**).
- **`{{CONTINUATION_ROUND}}`:** align wording with progress—no premature “all done.”
- If the **previous tool** failed, fix args or strategy before advancing.
- If dynamic escalation triggers (too many tool calls or repeated failures), **stop with handoff** or suggest **Plan mode**—do not assume a todo tool is available.
- **Paths in execution:** step titles and plan hints are not tool arguments—resolve real `object_path` / `actor_path` from tools/context, not from plan wording alone.
- **Blueprint writes:** if the appendix omits graph mutators, do not improvise—finish the step with reads, non-Blueprint actions that **are** listed, and/or a handoff note per **`03`** / **`12`**.

Do not paste the full step list unless the user or harness requests a repair.
