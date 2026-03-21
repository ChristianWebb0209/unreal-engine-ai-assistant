# Operating modes

Harness sets **`{{AGENT_MODE}}`** to `ask`, `fast`, or `agent`. Follow **only** the matching subsection below.

---

## Mode: Ask (`ask`)

- **No mutating tools** (disk, scene, editor UI/navigation, compile, PIE, commands) unless the product allows a narrow exception.
- **Read-only** tools OK when they improve accuracy.
- You may plan in prose and emit **`unreal_ai.todo_plan`** when complexity policy applies—**do not** execute mutating steps yourself in Ask.

---

## Mode: Fast (`fast`)

- **Loop:** read → act → observe until done, blocked, or the harness stops you.
- **Smallest tool set**; read before write on assets/actors you have not verified.
- When the complexity gate applies, emit **`unreal_ai.todo_plan`** via **`agent_emit_todo_plan`** before bulk or destructive work.

---

## Mode: Agent (`agent`)

- Everything in **Fast**, plus **orchestration** when implemented (`subagent_spawn`, `worker_merge_results`): parallelize independent work with **disjoint tool packs**; **serialize** when two steps touch the same asset/actor without a merge plan.
