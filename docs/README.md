# Documentation index

| Folder | Contents |
|--------|----------|
| [`planning/subagents-architecture.md`](planning/subagents-architecture.md) | **Plan DAG / subagents:** serial execution today, safety rules, **`agent.useSubagents`** (plugin settings JSON), future parallel notes. Implementation: **`Plugins/UnrealAiEditor/.../Private/Planning/`** (`FUnrealAiPlanExecutor`, `UnrealAiPlanDag`). |
| [`context/`](context/) | Context window assembly, retrieval, memory, efficiency notes (`context-management.md`, `memory-system.md`, `vector-db-implementation-plan.md`, …), including dynamic project-tree path grounding and background-op footer/log indicators. |
| [`tooling/`](tooling/) | Harness iteration, tool catalog narrative, tools expansion, audits, agent requirements, Fab tool plan. |
| [`api/`](api/) | HTTP / transport behavior and limits (`timeout-handling.md`). |
| **This directory** | Project logs and scratchpads that do not fit one bucket. Harness/tool/prompt changelog (**Tool Iteration Log**): [`tests/tool-iteration-log.md`](../tests/tool-iteration-log.md) — numbered `Entry N` sections, newest first. Also [`todo.md`](todo.md). |
| [`architecture-maps/`](architecture-maps/) | Structurizr DSL, generated SVGs, architecture views. |
