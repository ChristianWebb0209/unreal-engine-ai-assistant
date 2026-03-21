# Composable LLM prompt chunks (Unreal AI Editor)

**Location:** `Plugins/UnrealAiEditor/prompts/` (this plugin).

This folder holds **semantic fragments** the harness assembles into **system** and **developer** messages. The canonical machine-readable tool definitions live in [`Resources/UnrealAiToolCatalog.json`](../Resources/UnrealAiToolCatalog.json); narrative specs in the repo docs: [`tool-registry.md`](../../../docs/tool-registry.md), planning loop: [`complexity-assessor-todos-and-chat-phases.md`](../../../docs/complexity-assessor-todos-and-chat-phases.md).

## Design rules

- **One idea per file** so prompts can be cached (static prefix) and A/B-tested without duplicating prose.
- **Placeholders** use `{{LIKE_THIS}}`. The harness fills them from `FUnrealAiContextService`, `FUnrealAiComplexityAssessor`, and the active tool pack.
- **Order matters** roughly as numbered in `chunks/`; see the matrix below.

## Composition matrix

| Chunk | Ask | Fast | Agent | Notes |
|-------|-----|------|-------|--------|
| [`chunks/00-template-tokens.md`](chunks/00-template-tokens.md) | ✓ | ✓ | ✓ | Document placeholders only (no model text). |
| [`chunks/01-identity.md`](chunks/01-identity.md) | ✓ | ✓ | ✓ | Base role + Unreal editor scope. |
| [`chunks/02-operating-modes.md`](chunks/02-operating-modes.md) | inject **Ask** only | inject **Fast** only | inject **Agent** only | Shared preamble + one `## Mode:` block (`UnrealAiPromptBuilder::ExtractOperatingModeSection`). |
| [`chunks/03-complexity-and-todo-plan.md`](chunks/03-complexity-and-todo-plan.md) | ✓ (plan allowed; no mutating tools) | ✓ | ✓ | Always append `{{COMPLEXITY_BLOCK}}` from assessor. |
| [`chunks/04-tool-calling-contract.md`](chunks/04-tool-calling-contract.md) | ✓ (read tools only) | ✓ | ✓ | General tool discipline. |
| [`chunks/05-context-and-editor.md`](chunks/05-context-and-editor.md) | ✓ | ✓ | ✓ | Attachments, snapshot, `@` mentions. |
| [`chunks/06-execution-subturn.md`](chunks/06-execution-subturn.md) | when `activeTodoPlan` | same | same | Only when harness is in plan execution (summary + pointer). |
| [`chunks/07-safety-banned.md`](chunks/07-safety-banned.md) | ✓ | ✓ | ✓ | Permissions, destructive confirms, banned tools. |
| [`chunks/08-output-style.md`](chunks/08-output-style.md) | ✓ | ✓ | ✓ | Markdown, no fake chain-of-thought. |
| [`chunks/09-orchestration-workers.md`](chunks/09-orchestration-workers.md) | — | optional | ✓ | Level B: `subagent_spawn`, merge (future). |

## Runtime assembly (C++)

The editor builds the system/developer string at LLM time:

1. [`FUnrealAiContextService::BuildContextWindow`](../Source/UnrealAiEditor/Private/Context/FUnrealAiContextService.cpp) — formats `{{CONTEXT_SERVICE_OUTPUT}}`, runs [`FUnrealAiComplexityAssessor`](../Source/UnrealAiEditor/Private/Planning/UnrealAiComplexityAssessor.cpp) (feeds `{{COMPLEXITY_BLOCK}}`), and `ActiveTodoSummaryText` for `{{ACTIVE_TODO_SUMMARY}}`.
2. [`UnrealAiPromptBuilder::BuildSystemDeveloperContent`](../Source/UnrealAiEditor/Private/Prompt/UnrealAiPromptBuilder.cpp) — loads `prompts/chunks/*.md`, injects the mode slice from `02-operating-modes.md`, substitutes template tokens.
3. [`UnrealAiTurnLlmRequestBuilder::Build`](../Source/UnrealAiEditor/Private/Harness/UnrealAiTurnLlmRequestBuilder.cpp) — merges conversation history, tools JSON, model id, streaming flag, API URL/key from the selected model profile (`FUnrealAiAgentTurnRequest::ModelProfileId` from UI session).
4. [`FUnrealAiLlmInvocationService`](../Source/UnrealAiEditor/Private/Harness/UnrealAiLlmInvocationService.h) + [`ILlmTransport`](../Source/UnrealAiEditor/Private/Harness/ILlmTransport.h) — POST to `.../chat/completions` ([`FOpenAiCompatibleHttpTransport`](../Source/UnrealAiEditor/Private/Transport/FOpenAiCompatibleHttpTransport.cpp)).

## Typical assembly

**System message (conceptual):**

1. `01-identity`
2. Mode slice from `02-operating-modes`
3. `03` + filled complexity lines
4. `04`, `05`, `07`, `08`
5. If executing a stored plan: `06`
6. If Agent mode and workers enabled: `09`
7. Optional **`StaticSystemPrefix`** from context prepended when set (`SystemOrDeveloperBlock`). **`{{CONTEXT_SERVICE_OUTPUT}}`** is substituted inside chunks `03` / `05` from `BuildContextWindow`’s `ContextBlock`.

**Tools array:** Built from the catalog by mode (`BuildOpenAiToolsJsonForMode`); optional **narrow packs** per task—see [`tools/by-category.md`](tools/by-category.md) for human-readable grouping and [`tools/core-pack.md`](tools/core-pack.md) for default “always in core” IDs.

## Maintenance

- When **`UnrealAiToolCatalog.json`** changes, refresh [`tools/by-category.md`](tools/by-category.md) and [`tools/catalog-snapshot.tsv`](tools/catalog-snapshot.tsv) (TSV is optional; can be regenerated from JSON).
- When **`agent_emit_todo_plan`** (or equivalent) lands in the catalog, add its schema pointer to `03-complexity-and-todo-plan.md` and the tool list.
