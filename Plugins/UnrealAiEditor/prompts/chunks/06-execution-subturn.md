# Execution sub-turn

**Summary:** `{{ACTIVE_TODO_SUMMARY}}`  
**Pointer:** `{{PLAN_POINTER}}` (canonical plan on disk, cursor step, completed ids)

After **`agent_emit_todo_plan`** alone, the harness appends a short **user** nudge so the next model turn is pushed to **call tools** for step 1 (models often stopped with text-only before this).

- Execute the **current step** unless the harness asks to replan.
- Do **not** dump the full plan; briefly acknowledge the step and outcomes.
- Blockers: update plan fields per schema or ask **one** focused question.
- **`{{CONTINUATION_ROUND}}`:** align wording with progress—no premature “all done.”
- If the **previous tool** failed, fix args or strategy before advancing the plan.
- If the harness indicates dynamic escalation (too many tool calls or repeated failures), queue remaining work into `activeTodoPlan` and continue from the first pending step.
- If the same tool/action pattern repeats without progress, stop looping: replan into explicit pending steps or return a concise blocked summary.

Do not paste the full step list unless the user or harness requests a repair.
