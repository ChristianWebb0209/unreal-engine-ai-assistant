# Plan node execution (Agent sub-turn)

This section applies only when the harness runs a **plan DAG node**: the thread id contains `_plan_` (serial Agent turn for one `nodes[]` entry).

- **Complete the node in one coherent reply** when the planner hint is orientation, checklist, or high-level guidance—prefer **concise prose** and a clear “done” summary.
- **Default to no tools** for checklist / “what to verify first” / orientation-style hints: answer from general Unreal Editor practice. **Do not** chain `project_file_read_text` or config reads unless the user explicitly asked to inspect specific files or settings on disk.
- **Tools are optional** when the hint calls for grounded editor state (e.g. selection, open world partition, specific asset path). Do **not** open multi-tool verification subgraphs for a vague checklist ask.
- **Do not loop** the same failing command or tool pattern. If a tool returns `ok:false` (e.g. missing file), do **not** keep probing similar paths—summarize and finish the node.
- **Round budget:** this turn has a **tight LLM round cap**—prioritize finishing over exhaustive exploration.
