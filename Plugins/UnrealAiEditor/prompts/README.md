# Composable LLM prompt chunks (Unreal AI Editor)

**Harness iteration handoff (scripts, catalog, when to escalate):** [`docs/tooling/AGENT_HARNESS_HANDOFF.md`](../../../docs/tooling/AGENT_HARNESS_HANDOFF.md)

**Location:** `Plugins/UnrealAiEditor/prompts/` (this plugin).

This folder holds **semantic fragments** the harness assembles into **system** and **developer** messages. The canonical machine-readable tool definitions live in [`Resources/UnrealAiToolCatalog.json`](../Resources/UnrealAiToolCatalog.json); narrative specs in the repo docs: [`tool-registry.md`](../../../docs/tooling/tool-registry.md), context: [`context-management.md`](../../../docs/context/context-management.md). **Plan mode (DAG)** behavior is defined in **`chunks/plan/*.md`** (planner pass) and **`chunks/plan-node/*.md`** (executor turns), plus C++ under `Source/UnrealAiEditor/Private/Planning/`. The `agent_emit_todo_plan` tool is **deprecated** (not exposed to the model); legacy `activeTodoPlan` may still appear in context.

## Design rules

- **One idea per file** so prompts can be cached (static prefix) and A/B-tested without duplicating prose.
- **Placeholders** use `{{LIKE_THIS}}`. The harness fills them from `FUnrealAiContextService` and the active tool pack.
- **Order matters** roughly as numbered in `chunks/`; see the matrix below.
- **Canonical behavior:** **`01-identity.md`** (who you are + **Examples contract**) + **`04-tool-calling-contract.md`** (**Discovery before targeted calls**, schemas, minimal JSON). Other chunks address **chunk-specific** behavior or fix **local** example leakage—do not repeat the full invariant in every file.

## Composition matrix

| Chunk | Ask | Agent | Plan | Notes |
|-------|-----|-------|------|--------|
| [`chunks/00-template-tokens.md`](chunks/00-template-tokens.md) | ✓ | ✓ | ✓ | Document placeholders only (no model text). |
| [`chunks/01-identity.md`](chunks/01-identity.md) | ✓ | ✓ | ✓ | Base role + scope; identity + **Examples contract**. |
| [`chunks/02-operating-modes.md`](chunks/02-operating-modes.md) | inject **Ask** only | inject **Agent** only | inject **Plan** only | Shared preamble + one `## Mode:` block (`UnrealAiPromptBuilder::ExtractOperatingModeSection`). |
| [`chunks/03-complexity-and-todo-plan.md`](chunks/03-complexity-and-todo-plan.md) | ✓ (plan allowed; no mutating tools) | ✓ | ✓ | Complexity, scope, graceful handoff when blocked; Plan mode for structured DAGs. |
| [`chunks/04-tool-calling-contract.md`](chunks/04-tool-calling-contract.md) | ✓ (read tools only) | ✓ | ✓ | General tool discipline; Blueprint IR + in-process layout (`merge_policy`, `layout_scope`, `blueprint_format_graph`, `format_graphs`). |
| [`chunks/12-build-blueprint-delegation.md`](chunks/12-build-blueprint-delegation.md) | — | ✓ | — | Main agent: `<unreal_ai_build_blueprint>` handoff; graph mutations delegated to automated builder. |
| [`chunks/13-blueprint-builder-resume.md`](chunks/13-blueprint-builder-resume.md) | — | ✓ (one-shot after builder result) | — | `bInjectBlueprintBuilderResumeChunk`; pairs with structured builder `user` message. |
| [`chunks/blueprint-builder/`](chunks/blueprint-builder/) | — | **sub-turn only** | — | Alternate stack when `bBlueprintBuilderTurn` (deterministic loop, architecture notes, fail-safe). |
| [`chunks/05-context-and-editor.md`](chunks/05-context-and-editor.md) | ✓ | ✓ | ✓ | Attachments, snapshot, `@` mentions. |
| [`chunks/10-mvp-gameplay-and-tooling.md`](chunks/10-mvp-gameplay-and-tooling.md) | ✓ | ✓ | ✓ | MVP gameplay flows, PIE, matrix `ok:false` semantics (`UnrealAiPromptBuilder` after `05`). |
| [`chunks/plan-node/`](chunks/plan-node/) | — | when thread `*_plan_*` | — | Serial plan DAG node turns (`UnrealAiPromptAssemblyStrategy`). |
| [`chunks/06-execution-subturn.md`](chunks/06-execution-subturn.md) | when `activeTodoPlan` | same | same | Legacy persisted todo plan only (`summary + pointer`). |
| [`chunks/07-safety-banned.md`](chunks/07-safety-banned.md) | ✓ | ✓ | ✓ | Permissions, destructive confirms, banned tools. |
| [`chunks/08-output-style.md`](chunks/08-output-style.md) | ✓ | ✓ | ✓ | Markdown, no fake chain-of-thought. |
| [`chunks/plan/`](chunks/plan/) | — | optional | ✓ | Plan mode: `unreal_ai.plan_dag` JSON (`Private/Planning/FUnrealAiPlanExecutor`). |

## Runtime assembly (C++)

The editor builds the system/developer string at LLM time:

1. [`FUnrealAiContextService::BuildContextWindow`](../Source/UnrealAiEditor/Private/Context/FUnrealAiContextService.cpp) — formats `{{CONTEXT_SERVICE_OUTPUT}}` and `ActiveTodoSummaryText` for `{{ACTIVE_TODO_SUMMARY}}`.
2. [`UnrealAiPromptBuilder::BuildSystemDeveloperContent`](../Source/UnrealAiEditor/Private/Prompt/UnrealAiPromptBuilder.cpp) — [`UnrealAiPromptAssemblyStrategy`](../Source/UnrealAiEditor/Private/Prompt/UnrealAiPromptAssemblyStrategy.cpp) loads `prompts/chunks/*.md` linearly and injects the mode slice from `02-operating-modes.md` via `ExtractOperatingModeSection`. Template tokens: `ApplyTemplateTokens`.
3. [`UnrealAiTurnLlmRequestBuilder::Build`](../Source/UnrealAiEditor/Private/Harness/UnrealAiTurnLlmRequestBuilder.cpp) — merges conversation history, tools JSON, model id, streaming flag, API URL/key from the selected model profile (`FUnrealAiAgentTurnRequest::ModelProfileId` from UI session).
4. [`FUnrealAiLlmInvocationService`](../Source/UnrealAiEditor/Private/Harness/UnrealAiLlmInvocationService.h) + [`ILlmTransport`](../Source/UnrealAiEditor/Private/Harness/ILlmTransport.h) — submit the request; the bundled HTTP transport is [`FOpenAiCompatibleHttpTransport`](../Source/UnrealAiEditor/Private/Transport/FOpenAiCompatibleHttpTransport.cpp) (shared chat-completions JSON shape, not one vendor only).

## Typical assembly

**System message (conceptual):**

1. `01-identity`
2. Mode slice from `02-operating-modes`
3. `03` complexity / scope / handoff policy
4. `04`, `12` (Agent), optional `13` (one-shot after builder result), `05`, `10`
5. If executing a stored plan: `06`
6. `07`, `08`
7. If Plan mode / planner pass: `plan/*.md` (ordered)
8. Optional **`StaticSystemPrefix`** from context prepended when set (`SystemOrDeveloperBlock`). **`{{CONTEXT_SERVICE_OUTPUT}}`** is substituted inside chunks `03` / `05` from `BuildContextWindow`'s `ContextBlock`.

**Tools array:** Built from the catalog by mode (`BuildLlmToolsJsonArrayForMode`, chat-completions function tools); optional **narrow packs** per task—see [`tools/by-category.md`](tools/by-category.md) for human-readable grouping and [`tools/core-pack.md`](tools/core-pack.md) for default "always in core" IDs.

## Maintenance

- When **`UnrealAiToolCatalog.json`** changes, refresh [`tools/by-category.md`](tools/by-category.md) and [`tools/catalog-snapshot.tsv`](tools/catalog-snapshot.tsv) (TSV is optional; can be regenerated from JSON).
- **`agent_emit_todo_plan`** is deprecated in the catalog (not in the model tool list); legacy threads may still have `activeTodoPlan` on disk.
