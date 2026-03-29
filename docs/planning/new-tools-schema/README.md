# New tools schema (planning)

Design-target artifacts for a **schema-first**, **retrieval-optimized** Unreal AI tool catalog and a **resolver layer** that maps model output to existing C++ handlers with maximum fault tolerance.

| File | Purpose |
|------|---------|
| [`newtoolschema.json`](newtoolschema.json) | JSON Schema: catalog shape, extended `ToolDefinition`, consolidated parameter `$defs`, routing hints, settings envelopes, dispatch envelope. |
| [`resolver-architecture.md`](resolver-architecture.md) | How resolvers should change: multi-stage pipeline, alias maps, family dispatch, repair, telemetry — **intentionally over-engineered** to absorb model error. |
| [`04-tool-calling-contract.md`](04-tool-calling-contract.md) | Contract copy aligned with this schema (prompts chunk may mirror). |

Parent context: [`../optimized-tools-catalog-pitch.md`](../optimized-tools-catalog-pitch.md), [`../tools-catalog-settings-unification.md`](../tools-catalog-settings-unification.md).

Live catalog today: `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json` (not modified from this folder).
