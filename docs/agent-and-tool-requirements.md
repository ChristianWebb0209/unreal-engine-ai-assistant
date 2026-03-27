# Agent & Tool Requirements

This document consolidates architectural decisions, operating modes, subagent design (Level B), tool/context strategy, and game-engine-specific considerations for larger-scale agentic workflows. It is intended as a single reference for implementation and prioritization.

---

## 1. Purpose & constraints

### 1.1 Goals

- Support **larger-scale agentic tasks** (multi-step, long-running, possibly parallel work units).
- Accept **non–real-time latency** (e.g. minutes per major step); optimize for **correctness, cost, and clarity** over instant responses.
- Use **normal instruction-tuned models** where possible; **fine-tuning** is optional and mainly for format consistency or domain lock-in—not a prerequisite for tool use when using structured APIs or tight prompts + validation.

### 1.2 Latency assumption

When end-to-end work already takes **1–10+ minutes** (engine operations, cooks, heavy generation), **additional orchestration round-trips** (routing, subagent spawn, merge) are usually **negligible** relative to total time. Prioritize:

- Fewer failed / retried mega-runs
- Narrower tool sets and clearer contracts
- Observable runs (IDs, logs, merge rationale)

### 1.3 v1 indexing and retrieval (historical v1 scope)

> Status note (current plugin): this section documents the original v1 launch boundary.
> The current codebase now includes an optional local per-project vector index and retrieval path.
> For current behavior and implementation details, see `docs/vector-db-implementation-plan.md`.

- **v1 includes no vector search whatsoever** — no embeddings, no vector database (local or hosted), and no semantic / embedding-based codebase RAG.
- Context in v1 comes from **non-semantic sources** only, for example: user messages, **tool-mediated reads** (files, assets, editor state), **Unreal Asset Registry** and other **deterministic** engine APIs where applicable, simple **keyword/path** search if we add it, and **conversation history** within limits.
- **Future versions** may add optional **local vector stores** (e.g. Chroma or similar on disk per user), **hybrid retrieval** (e.g. BM25 + embeddings), or **semantic tool-description selection**; that work is **out of scope for v1** and should be a separate milestone with its own cost, privacy, and invalidation story.

**Rationale:** Ship a reliable agent harness, tool surface, and orchestration first; defer embedding pipelines, chunking strategy, index invalidation, and storage until there is a proven need.

### 1.4 MVP deployment (plugin-only — no server, no product backend)

Aligned with the MVP architecture, local-first mandate, and persistence model.

| Rule | Detail |
|------|--------|
| **Shipping unit** | **Unreal editor plugin only.** Chat UI, tool execution, persistence, and agent orchestration (including **Level B** parallel workers) run **inside the editor process** (with optional **child processes** spawned locally if needed—still **not** a separate *product* server). |
| **No product backend** | **No** hosted REST API, account service, chat relay, job queue, RAG host, or telemetry backend **for MVP**. Anything labeled “server” in integration docs means **localhost inside the plugin** (e.g. MCP) or **third-party** APIs. |
| **Outbound network** | **Only** what the user configures: **HTTPS** (or provider SDKs) to **third-party** LLM vendors (OpenRouter, Anthropic, OpenAI, etc.). No requirement to call **our** infrastructure. |
| **Inbound** | Optional **localhost** endpoints for MCP / external CLI tools—served by the **editor**, not the public internet. |
| **Data** | Chats, settings, logs: **local disk** under the user profile (see PRD §2.5). |
| **Future** | A **product** cloud backend (sync, team features, hosted retrieval) is **out of scope for MVP** and would be explicitly versioned and opt-in. Optional **local-only** vector stores remain consistent with “no backend” as long as they stay on disk inside the plugin’s data root. |

### 1.5 v1 differentiation: advanced tasks via structured planning

- **Competitive aim:** v1 should be **stronger than typical editor copilots on large, ambiguous, multi-step tasks**—a category where many products still **one-shot** or lightly tool-loop and appear “finished” before dependencies and acceptance criteria are handled.
- **Product bet:** Ship a **planning loop** in the plugin: **deterministic complexity assessor** → **validated `unreal_ai.todo_plan`** (tool-emitted, schema-checked) → **harness-driven execution sub-turns** with **hard rails** (max sub-turns, cancel, optional auto-continue toggle). **Execution prompts use summary + pointer** against the canonical plan on disk (no full-plan re-paste every roundtrip)—see [`docs/complexity-assessor-todos-and-chat-phases.md`](docs/complexity-assessor-todos-and-chat-phases.md).
- **Not a substitute for RAG later** (§1.3): this is **orchestration discipline first**; retrieval remains a separate milestone.

---

## 2. Operating modes

Three modes give predictable behavior, cost, and risk profiles.

### 2.1 Ask mode

| Aspect | Specification |
|--------|----------------|
| **Tools** | **None** (no side-effecting tool calls). |
| **Use case** | Q&A, planning prose, explaining concepts, reviewing proposals. |
| **Optional** | **v1:** No RAG — context from user text, pasted snippets, or **read-only tools** that pull explicit files/assets (still no mutating tools). **Future:** optional vector/RAG-based retrieval (see §1.3). |
| **Implementation** | Minimal: system prompt + user message; optional explicit reads only—**no embedding index in v1**. |

**Rationale:** Safe default; no accidental project/editor mutations; lowest surprise.

### 2.2 Fast mode

| Aspect | Specification |
|--------|----------------|
| **Tools** | **Enabled** — standard single-agent loop (read/search/edit/terminal/engine bridge as applicable). |
| **Use case** | Single-threaded tasks: one conversation stream, one model context, iterative tool use. |
| **Implementation** | Classic agent harness: plan → act → observe → repeat until stop condition or budget. When complexity policy triggers, **validated structured plan** + continuation (see §1.5) instead of one-shotting large work. |

**Rationale:** Default for “do work now” without spawning parallel workers.

### 2.3 Agent mode

| Aspect | Specification |
|--------|----------------|
| **Tools** | **Enabled** + **meta-tool(s)** to spawn **subagents** (Level B — see §3). |
| **Use case** | Large tasks decomposed into **parallel** or **isolated** work units with a **merge** phase. |
| **Implementation** | Fast mode **plus** orchestration: enqueue workers, collect structured results, merge, optionally continue. |

**Rationale:** Scales to broader tasks without one context window holding everything.

---

## 3. Subagents: Level B (parallel workers)

### 3.1 Definition

**Level B** means:

- A **single orchestrator** (parent run) decides **what** to parallelize and **how** to merge.
- Each **worker** is still an LLM (+ tools) but with:
  - **Isolated context** (fresh system + task brief, not full parent history unless explicitly summarized/injected).
  - Optionally a **role prompt** (“you specialize in X”).
  - Its own **tool allowlist** (tool pack) and **budget** (steps, tokens, time).
- **N concurrent** LLM calls may run; results are **merged** by deterministic logic and/or a **merge** model step.

This is **not** required to be separate OS processes initially; **logical isolation** (session ID, message list, tool pack) is enough.

### 3.2 What Level B is not (initially)

- **Not** infinite recursive subagent trees (that’s a later “Level D”–style complexity).
- **Not** mandatory separate fine-tuned models per worker—same model with different prompts/packs is fine.

### 3.3 Orchestrator responsibilities

1. **Decompose** the user goal into **independent** or **sequential** chunks where possible.
2. Assign each chunk: **objective**, **constraints**, **tool pack**, **budget**, **output schema**.
3. **Launch** workers (parallel up to `max_parallelism`).
4. **Collect** structured outputs (see §5).
5. **Merge**:
   - **Deterministic**: concatenate, union of file lists, take highest-priority conflict policy.
   - **Model-assisted**: single merge pass with **only** worker summaries + artifacts (not full transcripts).
6. **Resolve conflicts** if two workers touched the same resources (policy: serial lock, last-writer-wins, or reject and re-run).

### 3.4 Difficulty assessment

| Area | Notes |
|------|--------|
| Core harness (spawn + schema + collect) | **Medium** — mostly engineering, not ML. |
| Parallelism | **Medium** — concurrency, backpressure, cancellation. |
| Merge & conflict policy | **Medium–High** — product decisions + edge cases. |
| Observability | **Medium** — parent/child run IDs, structured logs. |

### 3.5 Risks & mitigations

| Risk | Mitigation |
|------|------------|
| Duplicate or conflicting edits | Resource scopes per worker; **serial** execution for conflicting files; or explicit locks. |
| Cost spikes | Per-worker **token/step budgets**; cap `max_parallelism`. |
| Parent context explosion | Hand off **summaries + artifacts**, not full child traces (unless debug mode). |
| Hard debugging | **Run graph**: `run_id`, `parent_run_id`, `worker_id`, tool calls, merge version. |

---

## 4. Tooling model: APIs, prompts, and fine-tuning

### 4.1 Vendor tool / function calling

Many production systems use **native tool APIs** (JSON Schema parameters, structured `tool_calls`). The model does not need custom fine-tuning to “know XML”—the channel constrains output.

### 4.2 Prompt-only tool specs (chat completion)

If only text-in/text-out is available:

- System prompt lists **tool names**, **minimal descriptions**, and **exact return format** (XML/JSON).
- **Validation + retry** on parse failure.
- **Fine-tuning** can improve adherence but is **not** always required.

### 4.3 Implication for this project

Prefer **structured tool definitions + validation**. Fine-tune only if metrics show systematic format drift or domain vocabulary gaps.

---

## 5. Contracts: parent ↔ worker handoff

### 5.1 Recommended structured child result

Workers should return a **fixed schema**, for example:

- `status`: `success` | `partial` | `failed`
- `summary`: short natural language
- `artifacts`: paths, URIs, or IDs of produced/changed objects
- `errors`: machine-readable codes + messages
- `follow_up_questions`: optional blockers for the parent

Parents should **not** rely on free-form-only replies for downstream automation.

### 5.2 Spawn parameters (meta-tool input)

- `goal` (string)
- `constraints` (e.g. paths, forbidden operations)
- `tool_pack_id` or explicit `allowed_tools[]`
- `max_steps` / `max_tokens` / wall-clock budget
- `output_schema` version
- `parallel_group_id` (for batching)

### 5.3 Depth & parallelism caps (v1)

- **`max_subagent_depth`**: start at **1** (parent → workers only).
- **`max_parallel_workers`**: start low (e.g. **2–4**); tune with cost and engine load.

---

## 6. Tool & context optimization

Because **tool descriptions can be tiny** and a **core set** may cover most work in a **small number of lines**, optimizations focus on **accuracy and cost**, not only latency.

**v1 note:** All strategies below are allowed **except** anything that requires **embeddings or a vector store** (see §1.3). Semantic / “tool RAG” rows apply to **post–v1** unless implemented with non-semantic heuristics only.

### 6.1 Strategies to implement or consider

| Strategy | Purpose |
|----------|---------|
| **Core tool block + appendix** | Always inject minimal **core** tools; load specialist packs **on demand** (second round or `NEED_TOOLS` signal). |
| **Two-phase routing** | Cheap pass: intent/tags → second pass with **only** relevant tool packs. |
| **Semantic tool retrieval (“tool RAG”)** | *Post–v1:* embed **short** tool descriptions; retrieve top-**k** when the catalog grows. **v1:** use rules, tags, or small static packs instead. |
| **Tool design discipline** | Prefer **many small tools** with **one-line** descriptions over few giant tools with long docs. |
| **Stable IDs + explicit parameter schemas** | Reduces bad calls and retries. |
| **Project-scoped tool subsets** | Omit tools that don’t apply (e.g. no PCG tools if unused). |
| **Trajectory / observation compaction** | Summarize or drop stale tool outputs in long runs. |
| **Prompt caching** (where supported) | Cache static tool catalog prefix to save cost when definitions are large. |

### 6.2 Known failure modes at scale

Research and practice suggest:

- Very **large** tool catalogs in a **single** prompt hurt **selection accuracy** and burn tokens.
- **Position effects** (“lost in the middle”) can affect long tool lists—**narrowing** beats raw ordering hacks.

### 6.3 Interaction with slow end-to-end latency

When engine/editor work dominates wall clock, prioritize:

- **Narrow tools** → fewer wrong actions → fewer multi-minute retries.
- **Dry-run / preview** tools where expensive operations exist.
- **Multi-round orchestration** is acceptable if it reduces failed runs.

---

## 7. Context strategies: code agents vs game engines

This section lists **reference patterns** used in the industry. **v1 does not implement vector-based or embedding-based retrieval** (§1.3); treat hybrid / agentic RAG rows as **future** options unless done with keywords, paths, and tool reads only.

### 7.1 General code AI agents

| Strategy | Notes |
|----------|--------|
| **Hybrid retrieval** | *Future:* embeddings + keyword (BM25). **v1:** keyword/path/symbol search + tool reads only. |
| **AST / structural chunks** | Signatures, imports, relevant subtrees vs whole files. |
| **Layered context** | Active (selection, errors) → local (file + deps) → global (README, architecture). |
| **Agentic RAG** | *Future:* retrieve → act → retrieve with embeddings. **v1:** iterative **tool** reads (not vector retrieve). |
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
| **Domain tool packs** | Materials, animation, networking, packaging—different packs per task. |

### 7.3 Fine-tuning vs prompting in engines

Many Unreal-oriented integrations use **standard models** + **structured tools** + **editor integration**; domain fine-tuning is an **accelerator**, not a prerequisite, if the harness is solid.

---

## 8. Observability & product requirements

### 8.1 Required for Agent mode + Level B

- **Run ID** per user session step; **parent_run_id** / **worker_id** for tree edges.
- **Structured log** of tool calls (name, args hash, duration, outcome).
- **Merge log**: which worker outputs were combined and how conflicts were resolved.
- **Budget usage**: tokens/steps per worker and aggregate.

### 8.2 User-facing

- Clear indication of **mode** (Ask / Fast / Agent).
- When **workers** run: progress (queued / running / merging), and **failure** with retry policy.

---

## 9. Phased implementation roadmap (suggested)

| Phase | Scope |
|-------|--------|
| **P0** | Ask + Fast; single-agent loop; core tool pack; validation + retries; **no vector search** (§1.3). |
| **P1** | Agent mode: **one-level** Level B subagents; structured handoff schema; merge v1 (deterministic + optional single merge model); caps on parallelism and depth. |
| **P2** | Tool routing (two-phase heuristics or tags; **optional post–v1:** semantic tool RAG); tool packs by domain; compaction of long traces. |
| **P3** | Conflict detection, file-level locking or serial fallback; richer merge policies. |
| **P4** | Deeper trees or higher parallelism only if metrics justify complexity. |
| **Future (post–v1)** | Optional **local vector DB** (e.g. per-user disk), codebase embeddings, hybrid retrieval — only after v1 is stable (§1.3). |

---

## 10. Open decisions (to fill in during implementation)

- [ ] Exact **tool pack** list and which modes may invoke which packs.
- [ ] **Merge** strategy: deterministic-only vs model-assisted vs hybrid.
- [ ] **Parallelism** default and max for Unreal/editor safety (single editor thread constraints may force **logical** parallelism with **serialized** mutating tool calls—validate against your bridge).
- [ ] Whether **Ask** mode uses only explicit reads / deterministic search (**v1** — no vector index per §1.3).
- [ ] Telemetry: **MVP default = none** or strictly local logs unless an opt-in policy is added later; no product telemetry backend per §1.4.

---

## 11. Related artifacts in this repo

- `README.md` — MVP summary (plugin-only, no product backend).
- `README.md` — product positioning, architecture summary, and build flow.
- `docs/context-service.md` — **context assembly service**: per-chat `context.json`, `BuildContextWindow`, no vector search in v1.
- `analysis/unreal_plugin_demand_shortlist.md` — demand-oriented plugin ideas from Reddit question posts (separate research track).

---

*Document version: 1.2 — adds **MVP plugin-only / no product backend** (§1.4). v1.1: no vector search (§1.3).*
