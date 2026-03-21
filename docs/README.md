# Documentation layout



## Product specs (source of truth)



| File | Description |

|------|-------------|

| **[`../PRD.md`](../PRD.md)** | Full product requirements: **MVP = plugin-only, no product backend/server**, local-first storage, features. |

| **[`../agent-and-tool-requirements.md`](../agent-and-tool-requirements.md)** | Agent modes, subagents, tool/context rules; **§1.4** restates MVP deployment. |

| **[`context-service.md`](context-service.md)** | Per-chat context assembly, `context.json` schema, integration points. |

| **[`agent-harness.md`](agent-harness.md)** | Harness entry points (`IUnrealAiAgentHarness`, `ILlmTransport`, `IToolExecutionHost`), `conversation.json`, Level-B merge. |

| **[`chat-renderer.md`](chat-renderer.md)** | Slate transcript UI: `FUnrealAiChatTranscript`, `IAgentRunSink`, tools/thinking/todos, streaming + typewriter, Stop. |

| **[`frontend-implementation-guide.md`](frontend-implementation-guide.md)** | **Client UI backlog:** inventory of tabs/menus, PRD §8–§9 gaps, prioritized Slate work vs docs. |

| **[`complexity-assessor-todos-and-chat-phases.md`](complexity-assessor-todos-and-chat-phases.md)** | **v1 planning loop:** complexity assessor, `unreal_ai.todo_plan`, summary+pointer execution, rails, chat phases — see [`agent-and-tool-requirements.md`](../agent-and-tool-requirements.md) §1.5. |

| **[`tool-registry.md`](tool-registry.md)** | Human-readable tool catalog (narrative + Epic links). **Machine catalog:** `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`. |



## `aik-editor/` (reference material)



The **`aik-editor/pages/**`** tree is **scraped / reference documentation** (e.g. Agent Integration Kit–style structure) used for parity research and UX patterns. It describes **third-party** products and flows; it is **not** automatically aligned with our MVP constraints.



For **our** architecture, always use **`PRD.md`** and **`agent-and-tool-requirements.md`**.



## Other



| Path | Description |

|------|-------------|

| [`../analysis/`](../analysis/) | Ad-hoc analysis artifacts (e.g. Reddit demand shortlist). |

| [`../Plugins/UnrealAiEditor/`](../Plugins/UnrealAiEditor/) | Editor plugin (includes tool catalog JSON + `Private/Tools/` execution). |

