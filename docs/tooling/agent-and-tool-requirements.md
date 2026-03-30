# Agent & Tool Requirements

This document consolidates architectural decisions, operating modes, tool/context strategy, and game-engine-specific considerations for larger-scale agentic workflows in the **Unreal AI Editor** plugin. It is intended as a single reference for implementation and prioritization.

---

## 1. Purpose & constraints

### 1.1 Goals

- Support **larger-scale agentic tasks** (multi-step, long-running work).
- Accept **nonâ€“real-time latency** (e.g. minutes per major step); optimize for **correctness, cost, and clarity** over instant responses.
- Use **normal instruction-tuned models** where possible; **fine-tuning** is optional and mainly for format consistency or domain lock-inâ€”not a prerequisite for tool use when using structured APIs or tight prompts + validation.

### 1.2 Latency assumption

When end-to-end work already takes **1â€“10+ minutes** (engine operations, cooks, heavy generation), extra **LLM round-trips** for planning or repair are usually **small** relative to total time. Prioritize:

- Fewer failed / retried mega-runs
- Narrower tool sets and clearer contracts
- Observable runs (IDs, logs, harness enforcement events)

### 1.3 v1 indexing and retrieval (historical v1 scope)

> Status note (current plugin): this section documents the original v1 launch boundary.
> The current codebase now includes an optional local per-project vector index and retrieval path.
> For current behavior and implementation details, see [`vector-db-implementation-plan.md`](../context/vector-db-implementation-plan.md).

- **v1 includes no vector search whatsoever** â€” no embeddings, no vector database (local or hosted), and no semantic / embedding-based codebase RAG.
- Context in v1 comes from **non-semantic sources** only, for example: user messages, **tool-mediated reads** (files, assets, editor state), **Unreal Asset Registry** and other **deterministic** engine APIs where applicable, simple **keyword/path** search if we add it, and **conversation history** within limits.
- **Future versions** may add optional **local vector stores** (e.g. Chroma or similar on disk per user), **hybrid retrieval** (e.g. BM25 + embeddings), or **semantic tool-description selection**; that work is **out of scope for v1** and should be a separate milestone with its own cost, privacy, and invalidation story.

**Rationale:** Ship a reliable agent harness and tool surface first; defer embedding pipelines, chunking strategy, index invalidation, and storage until there is a proven need.

### 1.4 MVP deployment (plugin-only â€” no server, no product backend)

Aligned with the MVP architecture, local-first mandate, and persistence model.

| Rule | Detail |
|------|--------|
| **Shipping unit** | **Unreal editor plugin only.** Chat UI, tool execution, persistence, and the agent harness run **inside the editor process** (optional local helpers are fine; **not** a separate *product* server). |
| **No product backend** | **No** hosted REST API, account service, chat relay, job queue, RAG host, or telemetry backend **for MVP**. Anything labeled â€œserverâ€ in integration docs means **localhost inside the plugin** (e.g. MCP) or **third-party** APIs. |
| **Outbound network** | **Only** what the user configures: **HTTPS** (or provider SDKs) to **third-party** LLM vendors (OpenRouter, Anthropic, OpenAI, etc.). No requirement to call **our** infrastructure. |
| **Inbound** | Optional **localhost** endpoints for MCP / external CLI toolsâ€”served by the **editor**, not the public internet. |
| **Data** | Chats, settings, logs: **local disk** under the user profile (see PRD Â§2.5). |
| **Future** | A **product** cloud backend (sync, team features, hosted retrieval) is **out of scope for MVP** and would be explicitly versioned and opt-in. Optional **local-only** vector stores remain consistent with â€œno backendâ€ as long as they stay on disk inside the pluginâ€™s data root. |

### 1.5 v1 differentiation: advanced tasks via structured planning

- **Competitive aim:** v1 should be **stronger than typical editor copilots on large, ambiguous, multi-step tasks**â€”a category where many products still **one-shot** or lightly tool-loop and appear â€œfinishedâ€ before dependencies and acceptance criteria are handled.
- **Product bet:** Ship a **planning loop** in the plugin: **deterministic complexity assessor** â†’ **validated `unreal_ai.todo_plan`** (tool-emitted, schema-checked) â†’ **harness-driven execution sub-turns** with **hard rails** (max sub-turns, cancel, optional auto-continue toggle). **Execution prompts use summary + pointer** against the canonical plan on disk (no full-plan re-paste every roundtrip)â€”see [`docs/complexity-assessor-todos-and-chat-phases.md`](docs/complexity-assessor-todos-and-chat-phases.md).
- **Not a substitute for RAG later** (Â§1.3): structured planning + harness rails come first; retrieval remains a separate milestone.

---

## 2. Operating modes

Ask and Agent modes give predictable behavior, cost, and risk profiles.

### 2.1 Ask mode

| Aspect | Specification |
|--------|----------------|
| **Tools** | **None** (no side-effecting tool calls). |
| **Use case** | Q&A, planning prose, explaining concepts, reviewing proposals. |
| **Optional** | **v1:** No RAG â€” context from user text, pasted snippets, or **read-only tools** that pull explicit files/assets (still no mutating tools). **Future:** optional vector/RAG-based retrieval (see Â§1.3). |
| **Implementation** | Minimal: system prompt + user message; optional explicit reads onlyâ€”**no embedding index in v1**. |

**Rationale:** Safe default; no accidental project/editor mutations; lowest surprise.

### 2.2 Agent mode

| Aspect | Specification |
|--------|----------------|
| **Tools** | **Enabled** via the normal harness tool loop (`unreal_ai_dispatch` / native tools). |
| **Use case** | Multi-step editing, asset/scene work, Blueprint IR, diagnostics; for structured DAG plans use **Plan mode** (`unreal_ai.plan_dag`). **`agent_emit_todo_plan`** is deprecated (not exposed to the model). |
| **Implementation** | Single **agent harness** in-process; **no** subagent spawn or worker-merge tools in this product build. |

**Rationale:** One conversation + tool surface keeps behavior predictable inside the editor.

---

## 3. Planning surfaces (implemented)

The plugin’s **structured** planning path is **Plan-mode DAG** (`FUnrealAiPlanExecutor`, schema **`unreal_ai.plan_dag`**; see [`../planning/subagents-architecture.md`](../planning/subagents-architecture.md) and prompt chunks **`09`/`11`**). Legacy **`unreal_ai.todo_plan`** persistence may still exist on disk.

| Mechanism | Entry | Code |
|-----------|--------|------|
| **Plan-mode DAG** | Plan chat: assistant emits **`unreal_ai.plan_dag`** JSON (no tools in planner pass). | `Private/Planning/UnrealAiPlanDag`, `Private/Planning/FUnrealAiPlanExecutor` |
| **Legacy todo plan** | **`agent_emit_todo_plan`** deprecated (not in model tool list); **`activeTodoPlan`** may load from old saves. | Context service + harness; summaries via `Private/Planning/UnrealAiStructuredPlanSummary` |

Plan DAG execution now uses deterministic ready-wave selection with guarded width policy (`agent.useSubagents` in `plugin_settings.json`, read via `FUnrealAiEditorModule::IsSubagentsEnabled()`), but child dispatch is currently single-turn serial. Plan-scoped status writes target the parent thread explicitly, and node failures are categorized for deterministic replan-vs-skip handling.

---

## 4. Tooling model: APIs, prompts, and fine-tuning

### 4.1 Vendor tool / function calling

Many production systems use **native tool APIs** (JSON Schema parameters, structured `tool_calls`). The model does not need custom fine-tuning to â€œknow XMLâ€â€”the channel constrains output.

### 4.2 Prompt-only tool specs (chat completion)

If only text-in/text-out is available:

- System prompt lists **tool names**, **minimal descriptions**, and **exact return format** (XML/JSON).
- **Validation + retry** on parse failure.
- **Fine-tuning** can improve adherence but is **not** always required.

### 4.3 Implication for this project

Prefer **structured tool definitions + validation**. Fine-tune only if metrics show systematic format drift or domain vocabulary gaps.

---

## 5. Multi-agent handoff (status)

This plugin build does not expose `subagent_spawn`, `worker_merge_results`, or a merge orchestrator. Tool results and plan state flow through the single harness + context service. Plugin setting `agent.useSubagents` currently gates wave-selection policy and diagnostics, not true concurrent child worker execution.

---

## 6. Tool & context optimization

Because **tool descriptions can be tiny** and a **core set** may cover most work in a **small number of lines**, optimizations focus on **accuracy and cost**, not only latency.

**v1 note:** All strategies below are allowed **except** anything that requires **embeddings or a vector store** (see Â§1.3). Semantic / â€œtool RAGâ€ rows apply to **postâ€“v1** unless implemented with non-semantic heuristics only.

### 6.1 Strategies to implement or consider

| Strategy | Purpose |
|----------|---------|
| **Core tool block + appendix** | Always inject minimal **core** tools; load specialist packs **on demand** (second round or `NEED_TOOLS` signal). |
| **Two-phase routing** | Cheap pass: intent/tags â†’ second pass with **only** relevant tool packs. |
| **Semantic tool retrieval (â€œtool RAGâ€)** | *Postâ€“v1:* embed **short** tool descriptions; retrieve top-**k** when the catalog grows. **v1:** use rules, tags, or small static packs instead. |
| **Tool design discipline** | Prefer **many small tools** with **one-line** descriptions over few giant tools with long docs. |
| **Stable IDs + explicit parameter schemas** | Reduces bad calls and retries. |
| **Project-scoped tool subsets** | Omit tools that donâ€™t apply (e.g. no PCG tools if unused). |
| **Trajectory / observation compaction** | Summarize or drop stale tool outputs in long runs. |
| **Prompt caching** (where supported) | Cache static tool catalog prefix to save cost when definitions are large. |

### 6.2 Known failure modes at scale

Research and practice suggest:

- Very **large** tool catalogs in a **single** prompt hurt **selection accuracy** and burn tokens.
- **Position effects** (â€œlost in the middleâ€) can affect long tool listsâ€”**narrowing** beats raw ordering hacks.

### 6.3 Interaction with slow end-to-end latency

When engine/editor work dominates wall clock, prioritize:

- **Narrow tools** â†’ fewer wrong actions â†’ fewer multi-minute retries.
- **Dry-run / preview** tools where expensive operations exist.
- **Multi-round** agent or planner passes are acceptable if they reduce failed runs.

---

## 7. Context strategies: code agents vs game engines

This section lists **reference patterns** used in the industry. **v1 does not implement vector-based or embedding-based retrieval** (Â§1.3); treat hybrid / agentic RAG rows as **future** options unless done with keywords, paths, and tool reads only.

### 7.1 General code AI agents

| Strategy | Notes |
|----------|--------|
| **Hybrid retrieval** | *Future:* embeddings + keyword (BM25). **v1:** keyword/path/symbol search + tool reads only. |
| **AST / structural chunks** | Signatures, imports, relevant subtrees vs whole files. |
| **Layered context** | Active (selection, errors) â†’ local (file + deps) â†’ global (README, architecture). |
| **Agentic RAG** | *Future:* retrieve â†’ act â†’ retrieve with embeddings. **v1:** iterative **tool** reads (not vector retrieve). |
| **Repository-level agents** | Search, read, edit, test loops; explicit handling of cross-file dependencies. |
| **Evaluation** | Measure whether retrieved context actually helps (quality gates). |

### 7.2 Game engine specifics

| Strategy | Notes |
|----------|--------|
| **Editor / engine bridge** | Tools that read **live state** (selection, world, assets), not only disk files. |
| **Multi-corpus indexing** | *Future:* multiple corpora + optional vectors. **v1:** deterministic catalogs (e.g. Asset Registry, explicit file lists) + tools; no semantic index. |
| **Graph / xref context** | Callers, callees, UCLASS/USTRUCT relationships for C++. |
| **Explicit attachments** | User points to GameObjects, assets, console output, screenshots (Unity-style patterns). |
| **Intermediate representations** | Plan in a compact DSL or graph **before** executing expensive or irreversible ops (PCG, bulk placement). |
| **Domain tool packs** | Materials, animation, networking, packagingâ€”different packs per task. |

### 7.3 Fine-tuning vs prompting in engines

Many Unreal-oriented integrations use **standard models** + **structured tools** + **editor integration**; domain fine-tuning is an **accelerator**, not a prerequisite, if the harness is solid.

---

## 8. Observability & product requirements

### 8.1 Required for Agent / Plan modes

- **Run / thread identifiers** suitable for correlating logs and harness artifacts.
- **Structured log** of tool calls (name, args hash, duration, outcome) where enabled.
- **Budget usage**: tokens/steps per turn (see settings and usage tracker).

### 8.2 User-facing

- Clear indication of **mode** (Ask / Agent / Plan).
- Plan mode: progress through planner vs node execution (`FUnrealAiPlanExecutor` continuation phases).

---

## 9. Phased implementation roadmap (suggested)

| Phase | Scope |
|-------|--------|
| **P0** | Ask + Agent; single-agent loop; core tool pack; validation + retries; **no vector search** (Â§1.3). |
| **P1** | Stronger planning rails (todo + Plan DAG), tool surface tiering, long-run harness quality. |
| **P2** | Tool routing (two-phase heuristics or tags; **optional postâ€“v1:** semantic tool RAG); tool packs by domain; compaction of long traces. |
| **P3** | Conflict detection, file-level locking or serial fallback where edits collide. |
| **P4** | Optional parallel plan-node execution or external workers **only** if metrics justify complexity. |
| **Future (postâ€“v1)** | Optional **local vector DB** (e.g. per-user disk), codebase embeddings, hybrid retrieval â€” only after v1 is stable (Â§1.3). |

---

## 10. Open decisions (to fill in during implementation)

- [ ] Exact **tool pack** list and which modes may invoke which packs.
- [ ] **Parallelism** default and max for Unreal/editor safety (mutating tools remain **serialized** on the game thread).
- [ ] Whether **Ask** mode uses only explicit reads / deterministic search (**v1** â€” no vector index per Â§1.3).
- [ ] Telemetry: **MVP default = none** or strictly local logs unless an opt-in policy is added later; no product telemetry backend per Â§1.4.

---

## 11. Related artifacts in this repo

- `README.md` â€” MVP summary (plugin-only, no product backend).
- `README.md` â€” product positioning, architecture summary, and build flow.
- `docs/context-service.md` â€” **context assembly service**: per-chat `context.json`, `BuildContextWindow`, no vector search in v1.
- `analysis/unreal_plugin_demand_shortlist.md` â€” demand-oriented plugin ideas from Reddit question posts (separate research track).

---

*Document version: 1.4 — subagent wave policy documented as `agent.useSubagents` in `plugin_settings.json` (not Project Settings `bUseSubagents`). v1.3 — removes subagent/worker-merge product scope; aligns with `Private/Planning/` plan DAG + todo summaries. v1.2: MVP plugin-only / no product backend (§1.4). v1.1: no vector search (§1.3).*
