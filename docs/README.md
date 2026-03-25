# Documentation index (`docs/`)

**Start here for harness / prompts / tools iteration:** [AGENT_HARNESS_HANDOFF.md](./AGENT_HARNESS_HANDOFF.md)

| Doc | Purpose |
|-----|---------|
| [AGENT_HARNESS_HANDOFF.md](./AGENT_HARNESS_HANDOFF.md) | Single entry: scripts, tiers, file map, escalation |
| [AGENT_HARNESS_TESTING.md](./AGENT_HARNESS_TESTING.md) | Console `UnrealAi.RunAgentTurn`, fixtures, `run.jsonl` |
| [LIVE_HARNESS.md](./LIVE_HARNESS.md) | Headed live qualitative runs (tool-goals manifest) |
| [CONTEXT_HARNESS.md](./CONTEXT_HARNESS.md) | Multi-turn context workflows + `DumpContextWindow` |
| [tool-goals.md](./tool-goals.md) | MVP gameplay-style prompt goals vs catalog |
| [TOOLING_FOLLOWUPS.md](./TOOLING_FOLLOWUPS.md) | Known tooling gaps |
| [context-management.md](./context-management.md) | Context state, `context.json`, budgets (architecture) |
| [chat-renderer.md](./chat-renderer.md) | Agent Chat Slate / transcript UI |
| [tool-registry.md](./tool-registry.md) | Human-readable tool narrative (canonical JSON: plugin `Resources/UnrealAiToolCatalog.json`) |
| [asset-type-coverage-matrix.md](./asset-type-coverage-matrix.md) | Dispatch/router coverage vs plan |
| [orchestrate-plan.md](./orchestrate-plan.md) | Orchestrate / DAG design notes (verify against current code) |
| [UnrealBlueprintFormatter-dependency.md](./UnrealBlueprintFormatter-dependency.md) | Formatter plugin sync / build |
| [UnrealBlueprintFormatter-remaining-todo.md](./UnrealBlueprintFormatter-remaining-todo.md) | Formatter checklist |
| [PRD-blueprint-formatter.md](./PRD-blueprint-formatter.md) | Blueprint IR / merge / layout PRD |
| [todo.md](./todo.md) | Internal backlog |

Plugin-facing overview: [Plugins/UnrealAiEditor/README.md](../Plugins/UnrealAiEditor/README.md).

**Automated tests / scenarios** under `tests/` are **maintainer-only** tooling to tune prompts, catalog, and dispatch—not an end-user product surface. See [tests/README.md](../tests/README.md).
