# Plan node — retries, blockers, budget

- **Do not loop** the same failing command or tool pattern. If a tool returns `ok:false` (e.g. missing file), do **not** keep probing similar paths—summarize and finish the node.
- **Repair once, then stop:** for validation failures (missing required fields, invalid argument shape, empty query, wrong path kind), do **at most one** corrected retry. If it still fails, emit a concise blocker and finish the node; do not issue a third near-identical call.
- If a tool error includes **`suggested_correct_call`**, use that exact inner tool_id + arguments on the next retry once; do not improvise multiple alternates before reporting blocker.
- **Classify blockers clearly:** when finishing blocked, include a one-line reason code in prose (`validation`, `tool_budget`, `stream_incomplete`, `transient_transport`, or `runtime`) so planner-side replan decisions remain deterministic.
- **Round budget:** this turn has a **tight LLM round cap** (plan-node Agent turns are limited to a **low** `PlanNodeMaxLlmRounds` in the product, typically on the order of **8** rounds)—prioritize finishing over exhaustive exploration.
