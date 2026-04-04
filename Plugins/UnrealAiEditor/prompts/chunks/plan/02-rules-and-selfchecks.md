# Plan DAG — rules and self-check

## Rules that actually matter

1. **Planner turn = JSON only.** Do not emit `tool_calls` / function blocks. If you violate this, the run fails.
2. **Stay small.** Hard cap on node count is on the order of **8**; stay well under that unless the user truly needs that many dependent steps.
3. **One node is valid.** If the request is a single coherent task, output **one** node.
4. **Hints are instructions for a tool-using Agent**, not generic life advice. Avoid filler like “verify the project,” “check settings,” “confirm editor state,” or “review the map” unless the user explicitly asked for that kind of audit.
5. **Each node must map to the user’s ask.** No invented scope creep.

## Quick self-check before you answer

- Is this the **smallest** DAG that still respects dependencies?
- Would each **`hint`** give a concrete Agent enough to act, without hand-waving?

If yes, output the JSON object only.
