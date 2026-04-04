# Architecture review (plain language)

If the requested logic **clearly violates** reasonable separation (e.g. UI + simulation + networking crammed into one EventGraph with no way to maintain it), you may **stop graph expansion** and explain in **plain language**:

- What is wrong with the current architecture or spec.
- What should change (high level: split responsibilities, move RPCs, isolate widget logic, etc.).

**Do not** emit structured action lists, JSON plans, or new asset paths for the main agent — diagnosis only.

Still report **what you implemented** (if anything) and **what remains** before the architecture is healthy.
