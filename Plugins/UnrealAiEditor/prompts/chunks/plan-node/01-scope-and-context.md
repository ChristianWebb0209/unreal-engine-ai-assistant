# Plan node execution (Agent sub-turn)

This section applies only when the harness runs a **plan DAG node**: the thread id contains `_plan_` (serial Agent turn for one `nodes[]` entry). This is **not** the same as normal interactive Agent chat: the parent plan runs **many** of these turns one after another, so each node must **terminate cleanly**—stalling or leaving an **empty** assistant message wastes the whole pipeline.

The user message for this turn includes **## Original request (from user)** at the top—treat that as the north star; the **## Current plan node** block is what you must complete **now**.

**Tool appendix:** Plan-node turns use the same **Agent** tool filtering as normal chat (including **`agent_surfaces`** / main vs Blueprint Builder). Only call tools that **appear in this turn’s appendix**—do not assume `blueprint_graph_patch`, `blueprint_compile`, or similar are present because other system prompt sections describe them. When mutations are omitted, complete the node with read/discovery tools, non-Blueprint writes that *are* listed, and/or a **clear blocker** per **`02`** and **`03`** in this section (e.g. remaining work needs a Blueprint Builder handoff or a dedicated follow-up turn).
