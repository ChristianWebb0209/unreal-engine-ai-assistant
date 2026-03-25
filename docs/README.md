# Documentation index (`docs/`)

**Start here for harness / prompts / tools iteration:** [AGENT_HARNESS_HANDOFF.md](./AGENT_HARNESS_HANDOFF.md)

| Doc | Purpose |
|-----|---------|
| [AGENT_HARNESS_HANDOFF.md](./AGENT_HARNESS_HANDOFF.md) | Harness tiers, console commands, deterministic fixtures, qualitative + context runs |
| [tool-goals.md](./tool-goals.md) | MVP gameplay-style prompt goals vs catalog |
| [context-management.md](./context-management.md) | Context state, `context.json`, budgets (architecture) |
| [chat-renderer.md](./chat-renderer.md) | Agent Chat Slate / transcript UI |
| [tool-registry.md](./tool-registry.md) | Human-readable tool narrative (canonical JSON: plugin `Resources/UnrealAiToolCatalog.json`) |
| [asset-type-coverage-matrix.md](./asset-type-coverage-matrix.md) | Dispatch/router coverage vs plan |
| [orchestrate-plan.md](./orchestrate-plan.md) | Orchestrate / DAG design notes (verify against current code) |
| [UnrealBlueprintFormatter.md](./UnrealBlueprintFormatter.md) | Formatter dependency + repo checklist |
| [todo.md](./todo.md) | Internal backlog |

Plugin-facing overview: [Plugins/UnrealAiEditor/README.md](../Plugins/UnrealAiEditor/README.md).

**Automated tests / scenarios** under `tests/` are **maintainer-only** tooling to tune prompts, catalog, and dispatch—not an end-user product surface. See [tests/README.md](../tests/README.md).
