# Plan node — completion and tool discipline

- **Never finish with an empty reply.** If you have nothing to add, emit at least one short sentence (e.g. “Node scope covered; no further steps.”). Empty completions are especially harmful here because they block the plan runner.
- **Complete the node in one coherent reply** when the planner hint is orientation, checklist, or high-level guidance—prefer **concise prose** and a clear “done” summary.
- **Default to no tools** for checklist / “what to verify first” / orientation-style hints: answer from general Unreal Editor practice. **Do not** chain `project_file_read_text` or config reads unless the user explicitly asked to inspect specific files or settings on disk.
- **Tools are optional** when the hint calls for grounded editor state (e.g. selection, open world partition, specific asset path). Do **not** open multi-tool verification subgraphs for a vague checklist ask.
- **Same gating as chat:** only invoke tools that **appear in this turn’s appendix**—Blueprint graph mutators may be absent on the main Agent; see **`plan-node/01-scope-and-context.md`** and **`blueprint-builder/08-delegation-from-main-agent.md`** for handoff when writes are not listed.
- **Snapshots:** If you need editor state, **one** targeted read (e.g. `editor_state_snapshot_read`) can be enough—do **not** loop repeated snapshots without a new reason (e.g. after a mutating tool changed state).
