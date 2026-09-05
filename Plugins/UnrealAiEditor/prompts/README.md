# Composable LLM prompt chunks (Unreal AI Editor)

**Harness iteration handoff (scripts, catalog, when to escalate):** [`docs/tooling/AGENT_HARNESS_HANDOFF.md`](../../../docs/tooling/AGENT_HARNESS_HANDOFF.md)

**Location:** `Plugins/UnrealAiEditor/prompts/` (this plugin).

This folder holds **semantic fragments** the harness assembles into **system** and **developer** messages. The canonical machine-readable tool definitions live in [`Resources/tools.main.json`](../Resources/tools.main.json) (plus merged [`tools.blueprint.json`](../Resources/tools.blueprint.json)); narrative specs in the repo docs: [`tool-registry.md`](../../../docs/tooling/tool-registry.md), context: [`context-management.md`](../../../docs/context/context-management.md). **Plan mode (DAG)** behavior is defined in **`chunks/plan/*.md`** (planner pass) and **`chunks/plan-node/*.md`** (executor turns), plus C++ under `Source/UnrealAiEditor/Private/Planning/`. Legacy persisted `activeTodoPlan` JSON may still appear in context from older sessions; there is no catalog tool to emit it anymore.

## Design rules

- **One idea per file** so prompts can be cached (static prefix) and A/B-tested without duplicating prose.
- **Placeholders** use `{{LIKE_THIS}}`. The harness fills them from `FUnrealAiContextService` and the active tool pack.
- **Layout:** shared prose lives in **`chunks/common/`**. **Orchestrator** (thin Agent lane) lives in **`chunks/orchestrator/`**. **Product specialists** live in **`chunks/specialists/<id>/`**, plus shared **`chunks/specialists/00-delegation-brief-token.md`** (`{{SPECIALIST_DELEGATION_BRIEF}}`) and **`chunks/specialists/00-resume-to-orchestrator.md`**. **Blueprint Builder** domain files are under **`chunks/blueprint-builder/`** (including main-agent delegation/resume for Blueprint). **World placement and environment/PCG building are not supported** — see repo README **Scope**. **Plan DAG** planner vs executor lives in **`chunks/plan/`** and **`chunks/plan-node/`**. **Order** matches `FUnrealAiLinearPromptAssemblyStrategy` (see below).
- **Canonical behavior:** **`common/01-identity.md`** (who you are + **Examples contract**) + **`common/04-tool-calling-contract.md`** (**Discovery before targeted calls**, schemas, minimal JSON; keep in sync with merged tool catalog). Other chunks address **chunk-specific** behavior or fix **local** example leakage—do not repeat the full invariant in every file.

## Composition matrix (main stack + shared files)

Paths are under `prompts/chunks/`. **Common** = `chunks/common/*.md`.

| Chunk | Ask | Agent | Plan | Notes |
|-------|-----|-------|------|--------|
| [`common/00-template-tokens.md`](chunks/common/00-template-tokens.md) | — | — | — | **Not assembled** — documents `{{PLACEHOLDER}}` tokens for authors; see `ApplyTemplateTokens` in source. |
| [`common/01-identity.md`](chunks/common/01-identity.md) | ✓ | ✓ | ✓ | Base role + scope; identity + **Examples contract**. |
| [`common/02-operating-modes.md`](chunks/common/02-operating-modes.md) | inject **Ask** only | inject **Agent** only | inject **Plan** only | Shared preamble + one `## Mode:` block (`ExtractOperatingModeSection`). |
| [`common/03-complexity-and-todo-plan.md`](chunks/common/03-complexity-and-todo-plan.md) | ✓ | ✓ | ✓ | Complexity, scope, graceful handoff when blocked; Plan mode for structured DAGs. |
| [`common/04-tool-calling-contract.md`](chunks/common/04-tool-calling-contract.md) | ✓ | ✓ | ✓ | General tool discipline; **appendix-first** routing; aligned with merged **`tools.main.json`** + **`tools.blueprint.json`**. |
| [`blueprint-builder/08-delegation-from-main-agent.md`](chunks/blueprint-builder/08-delegation-from-main-agent.md) | ✓ | ✓ | ✓ | **Main stack only** — `<unreal_ai_build_blueprint>` / `target_kind`; co-located with Blueprint Builder domain. |
| [`blueprint-builder/09-resume-on-main-agent.md`](chunks/blueprint-builder/09-resume-on-main-agent.md) | — | ✓ (one-shot) | — | `bInjectBlueprintBuilderResumeChunk`. |
| [`blueprint-builder/`](chunks/blueprint-builder/) (00–07, kinds) | — | **Blueprint sub-turn** | — | Full stack when `bBlueprintBuilderMode` (excludes 08–09 above from this stack). |
| [`orchestrator/`](chunks/orchestrator/) | — | **`bOrchestratorAgentTurn`** | — | Thin Agent lane: delegation protocol; see **Canonical assembly map** below. |
| [`specialists/`](chunks/specialists/) (`<id>/`, `00-delegation-brief-token.md`, `00-resume-to-orchestrator.md`) | — | specialist / resume | — | `ActiveProductSpecialistId`; orchestrator brief via `{{SPECIALIST_DELEGATION_BRIEF}}`. |
| [`common/05-context-and-editor.md`](chunks/common/05-context-and-editor.md) | ✓ | ✓ | ✓ | Attachments, snapshot, `@` mentions. |
| [`common/10-mvp-gameplay-and-tooling.md`](chunks/common/10-mvp-gameplay-and-tooling.md) | ✓ | ✓ | ✓ | MVP gameplay flows, PIE, matrix `ok:false` semantics. |
| [`plan-node/`](chunks/plan-node/) | — | when thread `*_plan_*` | — | Serial plan DAG node executor turns. |
| [`common/06-execution-subturn.md`](chunks/common/06-execution-subturn.md) | when `activeTodoPlan` | same | same | Legacy persisted todo plan only. |
| [`common/07-safety-banned.md`](chunks/common/07-safety-banned.md) | ✓ | ✓ | ✓ | Permissions, destructive confirms, banned tools. |
| [`common/08-output-style.md`](chunks/common/08-output-style.md) | ✓ | ✓ | ✓ | Markdown, no fake chain-of-thought. |
| [`plan/`](chunks/plan/) | — | optional | ✓ | Planner pass: `unreal_ai.plan_dag` JSON. |

## Runtime assembly (C++)

The editor builds the system/developer string at LLM time:

1. [`FUnrealAiContextService::BuildContextWindow`](../Source/UnrealAiEditor/Private/Context/FUnrealAiContextService.cpp) — formats `{{CONTEXT_SERVICE_OUTPUT}}` and `ActiveTodoSummaryText` for `{{ACTIVE_TODO_SUMMARY}}`.
2. [`UnrealAiPromptBuilder::BuildSystemDeveloperContent`](../Source/UnrealAiEditor/Private/Prompt/UnrealAiPromptBuilder.cpp) — [`UnrealAiPromptAssemblyStrategy`](../Source/UnrealAiEditor/Private/Prompt/UnrealAiPromptAssemblyStrategy.cpp) loads **`prompts/chunks/common/*.md`** for the shared prefix, plus **`prompts/chunks/<domain>/**`** for builder sub-turns and **`plan*/`** for DAG modes. Injects the mode slice from **`common/02-operating-modes.md`** via `ExtractOperatingModeSection`. Template tokens: `ApplyTemplateTokens`.
3. [`UnrealAiTurnLlmRequestBuilder::Build`](../Source/UnrealAiEditor/Private/Harness/UnrealAiTurnLlmRequestBuilder.cpp) — merges conversation history, tools JSON, model id, streaming flag, API URL/key from the selected model profile (`FUnrealAiAgentTurnRequest::ModelProfileId` from UI session).
4. [`FUnrealAiLlmInvocationService`](../Source/UnrealAiEditor/Private/Harness/UnrealAiLlmInvocationService.h) + [`ILlmTransport`](../Source/UnrealAiEditor/Private/Harness/ILlmTransport.h) — submit the request; the bundled HTTP transport is [`FOpenAiCompatibleHttpTransport`](../Source/UnrealAiEditor/Private/Transport/FOpenAiCompatibleHttpTransport.cpp) (shared chat-completions JSON shape, not one vendor only).

## Canonical assembly map (must match C++)

Source: [`FUnrealAiLinearPromptAssemblyStrategy::BuildSystemDeveloperContent`](../Source/UnrealAiEditor/Private/Prompt/UnrealAiPromptAssemblyStrategy.cpp). If you change load order there, update this section.

### Orchestrator Agent turn (`bOrchestratorAgentTurn`, Agent mode)

Thin delegation lane: **`chunks/common/01-identity.md`**, mode slice from **`common/02-operating-modes.md`**, **`chunks/orchestrator/00-overview.md`**, **`chunks/orchestrator/01-delegation-protocol.md`**, **`common/04-tool-calling-contract.md`**, Blueprint delegation + optional resume chunks (same as main stack), optional **`chunks/specialists/00-resume-to-orchestrator.md`** when `bInjectProductSpecialistResumeChunk`, then **`common/05`**, **`07`**, **`08`**, `ApplyTemplateTokens`, optional `SystemOrDeveloperBlock` prepend. Does **not** load **`common/03-complexity-and-todo-plan.md`** or **`common/10-mvp-gameplay-and-tooling.md`**.

### Product specialist sub-turn (`ActiveProductSpecialistId != None`)

**`chunks/common/01-identity.md`**, mode slice from **`common/02-operating-modes.md`**, then **`chunks/specialists/<id>/00-overview.md`** + **`01-scope.md`** for the active id (`scene`, `assets`, `viewport`, `diagnostics`, `playtest`, `animation`, `project-intel`, `editor-ui`, `settings`, `materials`), then **`chunks/specialists/00-delegation-brief-token.md`** (orchestrator brief via `{{SPECIALIST_DELEGATION_BRIEF}}`), then **`common/04`**, **`05`**, **`07`**, **`08`**, `ApplyTemplateTokens`, optional `SystemOrDeveloperBlock` prepend.

### Main / Ask / Agent / Plan stack (`!bBlueprintBuilderMode && !bOrchestratorAgentTurn && specialist None`)

All **numbered** files below live under **`prompts/chunks/common/`** unless noted.

| Step | Chunk(s) | Condition |
|------|-----------|-----------|
| 1 | `common/01-identity.md` | always |
| 2 | `common/02-operating-modes.md` | mode slice only (`ExtractOperatingModeSection`) |
| 3 | `common/03-complexity-and-todo-plan.md` | always |
| 4 | `common/04-tool-calling-contract.md` | always |
| 5 | `chunks/blueprint-builder/08-delegation-from-main-agent.md` | always (main-agent Blueprint handoff prose) |
| 6 | `chunks/blueprint-builder/09-resume-on-main-agent.md` | `bInjectBlueprintBuilderResumeChunk` |
| 7 | `common/05-context-and-editor.md` | always |
| 8 | `common/10-mvp-gameplay-and-tooling.md` | always |
| 9 | `chunks/plan-node/01` … `03` | `bIncludePlanNodeExecutionChunk` |
| 10 | `common/06-execution-subturn.md` | `bIncludeExecutionSubturnChunk` |
| 11 | `common/07-safety-banned.md` | always |
| 12 | `common/08-output-style.md` | always |
| 13 | `chunks/plan/01` … `04` | `bIncludePlanDagChunk` |
| — | `ApplyTemplateTokens` | then prepend optional `SystemOrDeveloperBlock` |

**Not in this stack:** Blueprint Builder sub-turn domain files (`blueprint-builder/00–07`, `kinds/`), orchestrator (`chunks/orchestrator/`), product specialist (`chunks/specialists/<id>/`), and `common/00-template-tokens.md` (authoring only).

### Blueprint Builder sub-turn (`bBlueprintBuilderMode`)

| Step | Chunk(s) |
|------|-----------|
| 1 | `common/01-identity.md` |
| 2 | `common/02-operating-modes.md` (mode slice) |
| 3–9 | `blueprint-builder/00-overview.md` … `03-fail-safe-handoff.md`, `05-verification-ladder.md` … `07-graph-patch-canonical.md` (fixed order; T3D/IR chunk removed—see `07` Catalog scope) |
| 11 | `blueprint-builder/kinds/<target_kind>.md` from handoff YAML (`UnrealAiBlueprintBuilderTargetKind`) |
| 12 | `common/05-context-and-editor.md` |
| 13 | `common/07-safety-banned.md` |
| 14 | `common/08-output-style.md` |
| — | `ApplyTemplateTokens`; prepend optional `SystemOrDeveloperBlock` |

**Not loaded in this sub-turn:** `blueprint-builder/08-delegation-from-main-agent.md` and `09-resume-on-main-agent.md` (those are for the **main** agent stack only).

### Main Agent vs Builder surfaces (product)

- **Agent orchestrator** turns (`bOrchestratorAgentTurn`, specialist `None`): tool gate allow-list in [`UnrealAiOrchestratorToolPolicy`](../Source/UnrealAiEditor/Private/Tools/UnrealAiOrchestratorToolPolicy.h) (read-only snapshot + log); substantive work uses **`<unreal_ai_delegate specialist="…">`** (see **`chunks/orchestrator/`**) or **`<unreal_ai_build_blueprint>`**.
- **Product specialist** sub-turns: [`UnrealAiProductSpecialistToolPolicy`](../Source/UnrealAiEditor/Private/Tools/UnrealAiProductSpecialistToolPolicy.cpp) + merged catalog categories; orchestrator brief is copied into the specialist system prompt via **`{{SPECIALIST_DELEGATION_BRIEF}}`**.
- **Ask / Plan** modes and **`_plan_` thread** workers: not on the orchestrator-only path; plan workers keep the broader Agent surface until plan-mode is rebuilt.
- **Substantive Blueprint graph mutations** on the default path: **`<unreal_ai_build_blueprint>`** with YAML **`target_kind`** → builder stack + domain-filtered tools.
- **World / environment placement** is **not** supported (no actor spawn/move, no PCG/landscape/foliage tools).
- **Escape hatch:** when `bOmitMainAgentBlueprintMutationTools` is false, surface gating is bypassed (power users).

## Typical assembly (summary)

**System message (conceptual, main stack):** `common/01` → `common/02` slice → `common/03` → `common/04` → `blueprint-builder/08-delegation` → optional `blueprint-builder/09-resume` → `common/05` → `common/10` → optional `plan-node/*` → optional `common/06` → `common/07` → `common/08` → optional `plan/*` → template substitution. Optional **`StaticSystemPrefix`** prepended via `SystemOrDeveloperBlock`. **`{{CONTEXT_SERVICE_OUTPUT}}`** is substituted inside chunks such as `03` / `05` from `BuildContextWindow`.

**Tools array:** Built from the catalog by mode (`BuildLlmToolsJsonArrayForMode`, chat-completions function tools); optional **narrow packs** per task—see [`tools/by-category.md`](tools/by-category.md) for human-readable grouping and [`tools/core-pack.md`](tools/core-pack.md) for default "always in core" IDs.

## Maintenance

- When **`tools.main.json`** or fragment catalogs change, refresh [`tools/by-category.md`](tools/by-category.md) and [`tools/catalog-snapshot.tsv`](tools/catalog-snapshot.tsv) (TSV is optional; can be regenerated from merged JSON).
- **`activeTodoPlan` on disk:** older threads may still carry this blob; it is not tied to a current catalog tool.
