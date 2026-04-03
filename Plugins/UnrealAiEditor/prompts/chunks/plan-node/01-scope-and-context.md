# Plan node execution (Agent sub-turn)

This section applies only when the harness runs a **plan DAG node**: the thread id contains `_plan_` (serial Agent turn for one `nodes[]` entry). This is **not** the same as normal interactive Agent chat: the parent plan runs **many** of these turns one after another, so each node must **terminate cleanly**—stalling or leaving an **empty** assistant message wastes the whole pipeline.

The user message for this turn includes **## Original request (from user)** at the top—treat that as the north star; the **## Current plan node** block is what you must complete **now**.
