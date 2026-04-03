# Unreal AI Editor Tooling: Failure Analysis & Implementation Playbook

This document serves two roles:

1. **Engineering truth:** why prior LLM-driven Unreal tooling failed, and the architectural principles that still apply.
2. **Playbook for a high-reasoning implementer:** a ground-up rebuild plan for **schema (JSON)**, **resolvers**, and **C++ implementation**, aligned with **top‑K tool retrieval** and **accuracy-first** goals.

**Audience:** treat this as the primary instruction set for a long, token-heavy session (on the order of **~1 hour of focused work** and **very large context usage**). Work in phases; commit or checkpoint after each phase.

**Related artifacts (read as needed, do not duplicate blindly):**

* [`docs/planning/new-tools-schema/README.md`](new-tools-schema/README.md) — schema-first targets, `newtoolschema.json`, resolver architecture notes.
* [`docs/planning/new-tools-schema/resolver-architecture.md`](new-tools-schema/resolver-architecture.md) — multi-stage dispatch, repair, telemetry.
* [`docs/tooling/tool-registry.md`](../tooling/tool-registry.md) — catalog ↔ execution mapping.
* [`docs/tooling/tool-dispatch-inventory.md`](../tooling/tool-dispatch-inventory.md) — handler modules and aliases.
* **Live catalog:** `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`
* **Dispatch / execution:** `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/` (`UnrealAiToolDispatch*.cpp`, `FUnrealAiToolExecutionHost.cpp`)

---

## Part A — Why the old approach broke (keep these lessons)

### A.1 Overly low-level *agent-facing* tooling

**Anti-pattern when exposed raw to the model:**

* `AddNode`, `ConnectPin`, `SetPinValue` as *unstructured* steps with no deterministic bookkeeping.

These force the model to act as a graph engine: track identities, ordering, and Unreal pin semantics. That fails often and compounds errors.

**Still valid engineering underneath:** a deterministic subsystem may *implement* low-level edits, but the **contract** to the LLM should not be “guess pin names and GUIDs without guardrails.”

### A.2 Lack of deterministic execution

If tool execution depends on implicit editor state, returns vague success, or skips validation, the run becomes non-recoverable.

**Required:** validation, structured success/failure, actionable errors, and idempotency where feasible.

### A.3 Missing or weak feedback loops

Without execution results, errors, and updated state summaries, the model cannot recover. **Hallucinated success** is a system bug, not a model quirk.

### A.4 Context overload (project-scale)

Indexing whole projects adds noise and latency. **Tooling context** should stay minimal; **retrieval** (top‑K tools, RAG, etc.) carries the rest.

### A.5 Unreal as a stateful system

Hidden editor state, object references, and mutable graphs break stateless tool assumptions.

**Implication:** the **runtime** owns references, patch sequencing, and validation—not the LLM.

---

## Part B — Architectural principles (non-negotiable)

### B.1 LLM as planner; engine as executor

The model selects tools and fills **schema-bound** parameters. It does **not** invent internal node IDs, silently fix broken graphs, or replace a validation layer.

### B.2 Deterministic execution layer

All mutations go through code that validates, applies, and returns structured results (including partial success only when explicitly designed).

### B.3 Two complementary surfaces (resolve the “low-level vs high-level” tension)

You want **broad Blueprint coverage** (essentially arbitrary K2 nodes) *and* **high accuracy**. Those goals are not met by either “only micro tools” or “only ten magic macros.” Use **both**:

| Layer | Purpose | LLM-facing? |
|-------|---------|----------------|
| **Macro / family tools** | Parameterized, consolidated operations for common workflows (line traces, branches, timers, spawners, etc.). | Yes — prefer when they match intent. |
| **Graph / IR / introspection tools** | Deterministic Blueprint edit pipeline: export → mutate → validate → compile; optional pin listing, patch ops, or structured IR. | Yes — when macros do not fit or user needs exotic nodes. |

**Key idea:** low-level *mechanics* can live **below** the tool boundary, as long as the **schema** gives the model stable handles (paths, patch IDs, merge policies, `suggested_correct_call`, etc.).

### B.4 Abstract references and stable handles

Prefer tool designs where the model refers to **assets, graph names, patch scopes, and outputs of previous tool calls**—not ad-hoc pin wiring in prose.

---

## Part C — Scale, retrieval, and “how many tools?”

### C.1 You can afford a large catalog

With **embedding + top‑K** (and good metadata), a **large** tool list is acceptable: the model usually sees a **relevant subset**, not the full enumeration.

### C.2 Large catalog ≠ redundant sprawl

**Avoid:** dozens of tools that differ only by a literal enum value where one parameterized tool would do.

**Prefer:**

* **Parameterized consolidation** for families of operations that share structure.
* **Distinct tool entries** when semantics, prerequisites, or failure modes genuinely differ (different summaries/tags/embeddings).

### C.3 Retrieval quality is part of the product

For every tool:

* **Summary:** one dense sentence naming *what* and *when to use*.
* **Tags / `domain_tags`:** synonyms users and models say (“line trace”, “visibility”, “AI perception”, “widget”, “timeline”).
* **Negative hints where useful:** “not for materials” / “requires Blueprint open” / “read-only”.

Assume **top‑K will fail** if tools are vague duplicates. Invest in **semantic separation**.

### C.4 Blueprint coverage goal

**Target:** the system should support building graphs that use **the vast majority of engine Blueprint nodes**, not a toy subset.

**Practical strategy:**

1. **IR / patch / compile loop** as the **broad** path (export → edit → validate → compile).
2. **Specialized tools** where IR is painful or error-prone but you can wrap Unreal APIs safely.
3. **Node listing / pin introspection** tools where they reduce hallucinated pin names.

Do **not** promise “one tool per node” unless you have automation to generate and maintain that catalog; generated surfaces must still have **deterministic validation** and **tests**.

---

## Part D — Implementation playbook (ground-up rebuild)

**Operational status (inventory + agent contracts):** see [tooling-inventory-status.md](tooling-inventory-status.md) and [deterministic-tool-contracts.md](deterministic-tool-contracts.md). Catalog structural CI: `scripts/Validate-UnrealAiToolCatalog.ps1`.

**Goal:** rebuild **schema JSON**, **resolvers**, and **handlers** so they are consistent, testable, and aligned with Parts B–C. **Preserve** anything already correct (behavior, edge-case handling, security gates); **replace** ad-hoc or contradictory contracts.

Work in order. Do not skip validation gates.

### Phase 0 — Inventory and “keep” list (30–60 minutes of reading/code)

1. Load `UnrealAiToolCatalog.json`: note `meta`, `status_legend`, permission/modes patterns.
2. Map tools to code: `tool-registry.md`, `tool-dispatch-inventory.md`, `UnrealAiToolDispatch*.cpp`.
3. List **known-good** behaviors (e.g., stable read tools, search tools, PIE tools) to **avoid regressions**.
4. Note **Blueprint** paths: `blueprint_export_ir`, `blueprint_apply_ir`, `blueprint_compile`, `blueprint_graph_patch` / graph helpers — whichever exist today are your **baseline** unless the playbook explicitly replaces them.

**Deliverable:** short bullet list: **Keep** / **Refactor** / **Replace** per major family.

### Phase 1 — Schema-first redesign (`newtoolschema.json` + catalog)

1. Align catalog shape with `docs/planning/new-tools-schema/newtoolschema.json` (or update that file if the redesign supersedes it—**one source of truth**).
2. For each tool family, define:

   * **Minimal JSON Schema** (`additionalProperties: false` where possible).
   * **Canonical parameter names** across tools (`object_path`, `blueprint_path`, `actor_path`—see `04-tool-calling-contract.md`).
   * **Explicit error shapes** (e.g., `suggested_correct_call`, field-level hints).

3. Split **macro tools** vs **graph/IR tools** clearly in `category` and `tags` for retrieval.

**Deliverable:** updated **`UnrealAiToolCatalog.json`** (and generated/derived schema if your pipeline uses one).

### Phase 2 — Resolver and dispatch layer

Follow `resolver-architecture.md` concepts even if you simplify naming:

* Normalize aliases at the boundary.
* Validate before touching the editor.
* Return **structured** errors for model recovery.
* Log **telemetry** (tool id, latency, validation failures) for later tuning.

**Deliverable:** consistent behavior in **`FUnrealAiToolExecutionHost`** and **`UnrealAiToolDispatch*.cpp`**; no duplicate conflicting validation.

### Phase 3 — Handler implementation

1. Implement or refactor C++ handlers so each tool’s **observable behavior** matches the schema.
2. Prefer **one authoritative code path** per logical operation (avoid copy-paste validators).
3. For Blueprint graph operations: enforce **ordering** (e.g., export freshness, merge policy, compile prerequisites).

**Deliverable:** compiles clean; run `./build-editor.ps1 --Headless` per project convention.

### Phase 4 — Retrieval: tags, summaries, and optional tooling

1. Audit **every** tool’s summary + tags for embedding quality.
2. If the picker uses a **core pack**, keep it small; rely on top‑K for long tail.
3. Add **pairing hints** in summaries (read X before write Y) where it reduces failure rates.

### Phase 5 — Verification

1. **Matrix / unit tests** if present (`UnrealAiToolCatalogMatrixRunner`, etc.)—extend for new schemas.
2. **Manual or harness scenarios:** Blueprint create → edit via IR → compile → PIE smoke.
3. Classify failures: **planning** vs **execution** vs **state** (see Part E).

**Done when:** new failures are **actionable** (clear error + suggested next tool or args), not silent corruption.

---

## Part E — Debugging framework (during and after rebuild)

Classify every failure:

| Type | Symptom | Fix direction |
|------|---------|----------------|
| **Planning** | Wrong tool or wrong intent | Summaries, tags, examples, tool family boundaries |
| **Execution** | Handler bug or Unreal API misuse | C++ fix, tests |
| **State** | Stale graph, wrong path, wrong asset | Stronger reads, export before write, discovery tools |

---

## Part F — Fine-tuning and models

Fine-tuning is **not** the first fix. After tools are deterministic:

* Prefer better descriptions, tags, and a few prompt examples.
* Consider higher-capability models for **planning** turns once the **contract** is honest.

---

## Part G — Target system characteristics

* High reliability on **realistic** Blueprint tasks, not demo-only paths.
* Predictable execution and **honest** errors.
* Clear separation: **planning** (LLM) vs **mutation** (engine).
* Scalable catalog: **large** but **retrieval-shaped**, not a flat bag of duplicates.

---

## Part H — Mindset

Do not build “an LLM that *is* Unreal.”

Build **deterministic Unreal systems that a model can invoke safely**—with **macro tools** where they shine and **graph/IR tools** when the user needs the full node space.

---

## Part I — Playbook constraints (for the executing agent)

* **Budget:** expect **long, multi-step** implementation; use **large** reasoning context for schema + dispatch + handlers together.
* **Preserve working pieces:** do not rewrite search, PIE, or read-only tools without cause.
* **Consistency:** schema, resolver messages, and handler behavior must tell the **same story**.
* **Build:** run **`./build-editor.ps1 --Headless`** periodically; use **`./build-editor.ps1 -Restart`** if the plugin DLL is locked (**LNK1104**).
* **UE version:** follow **UE 5.7** syntax and APIs per project rules.

---

## Conclusion

The primary bottleneck in LLM-driven Unreal tooling is **system design**: deterministic execution, honest contracts, and retrieval-aware catalog structure.

A **large** tool surface is compatible with that—as long as **duplication is semantic**, not cosmetic, and **Blueprint breadth** is achieved through **validated graph pipelines**, not through pushing raw graph trivia onto the model without guardrails.

Increasing model capability **helps planning** once the **tooling tells the truth**. The rebuild in Parts D–I is how you get there.
