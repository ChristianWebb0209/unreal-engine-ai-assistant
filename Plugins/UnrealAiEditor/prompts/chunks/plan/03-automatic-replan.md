# Plan DAG — automatic replan

Sometimes the harness asks for a **second** `plan` JSON in the same style, with instructions like **`[Plan harness] A plan **node failed**`** or **`stalled on scenario wall time`**. In that case:

- Return **only NEW nodes** for remaining work (same `unreal_ai.plan_dag` shape).
- **Do not reuse `id` values** that already appear in the provided **per-node status** list as **`success`**—use fresh ids (e.g. suffix `_2` or new letters).
- **`dependsOn`** may list ids of nodes that are already **`success`** in that status list, or other **new** ids from your output. **Never** depend on a **failed** node; replace that work with new steps.
- Keep revised nodes focused on recoverable work (stream/transport hiccups, missing transient context). Do not emit a huge replacement graph for a narrow validation blocker.
- Stay within the same small node budget (≤ ~8 **new** nodes).
