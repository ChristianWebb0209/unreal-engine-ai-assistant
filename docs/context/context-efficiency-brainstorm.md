# Prompt chunking, pruning, and context efficiency

This document captures **design and trade-off thinking** for reducing token cost and protecting quality: how static instructions are composed, where tokens actually go, how we might evolve chunking, and how to **measure** pruning safely.

**Canonical references (not duplicated in full here):**

- Runtime context assembly, ranking, persistence: [`context-management.md`](context-management.md)
- Chunk list, mode matrix, and assembly pipeline: [`Plugins/UnrealAiEditor/prompts/README.md`](../../Plugins/UnrealAiEditor/prompts/README.md)
- Tool surface vs context (orthogonal concerns): [`tools-expansion.md`](../tooling/tools-expansion.md)

---

## 1. Why this matters

- **Input tokens** (system prompt, history, `tools` JSON, embedded context) accumulate every turn and dominate **upload size** and **cache-prefix** behavior.
- **Output tokens** are often **priced higher per token** than input on many providers—so naive “make the model write less” looks attractive—but **cutting assistant verbosity or constraining output** tends to **hurt task success** faster than trimming redundant *instructions*, because the model still needs room for plans, tool arguments, and user-visible explanations.

**Practical bias:** prefer **pruning static instructions** and **avoiding duplicate or low-signal context** before squeezing answer length—unless the product explicitly wants shorter replies.

---

## 2. Where tokens go (rough mental model)

A chat-completions-style request is not one blob: the API carries **messages** (including a large **system** message) and often a separate **`tools`** array.

| Bucket | What it is | Order of magnitude (repo-specific) |
|--------|------------|-------------------------------------|
| **Static instructions** | Markdown chunks merged by `UnrealAiPromptBuilder::BuildSystemDeveloperContent` (identity, mode slice, complexity policy, tool contract, safety, style, optional plan/execution chunks) | Tens of thousands of characters; **`04-tool-calling-contract.md` is the single largest chunk** by far. |
| **Live editor context** | `BuildContextWindow` output substituted into the system string as `{{CONTEXT_SERVICE_OUTPUT}}` | Bounded by the context service / ranker; **harness traces** often show **low single-digit thousands of characters** for `ContextBlock` under typical budgets—not the full prompt. |
| **Tools JSON** | Function-tool definitions for the model (native full catalog vs narrowed pack vs dispatch surface) | Can be **very large** (on the order of **tens of thousands of characters** for a full native surface); **Plan mode** forces an empty tool list for the planner pass. |
| **Conversation history** | User / assistant / tool messages in the thread | **Small early**; grows with thread length; the turn builder **drops older messages** when estimated character budget is exceeded. |

**Important:** Long-running harness artifacts like `context_window_*.txt` and `context_build_trace_*.json` record **context service output** and diagnostics—they do **not** by themselves record full request size or a clean split between “instructions vs tools vs history.” For **measured** splits, correlate with request-build logging or transport logs (e.g. serialized body size and tools size), not only context dumps.

---

## 3. How static chunks are assembled today

- **Fixed sequence** is implemented in C++ (`UnrealAiPromptBuilder::BuildSystemDeveloperContent`), not by keyword routing: load numbered chunks, inject the **single mode subsection** from `02-operating-modes.md`, append **06** and **09** only when their conditions hold.
- **Template tokens** (`{{CONTEXT_SERVICE_OUTPUT}}`, `{{ACTIVE_TODO_SUMMARY}}`, etc.) are substituted after concatenation.

**Duplicate substitution:** `{{CONTEXT_SERVICE_OUTPUT}}` appears **twice** in `05-context-and-editor.md`; the implementation replaces **every** occurrence with the same `ContextBlock`. That means the **live context footprint inside the system message is effectively doubled** unless the template is revised—worth remembering when reasoning about “context size,” not just `ContextBlock` length from traces.

---

## 4. Chunking directions (future-friendly)

Nothing here is a commitment to implement; it is a shared vocabulary for iteration.

| Idea | Rationale |
|------|-----------|
| **Tighter prose in place** | Fastest win: same files, fewer words; **no new chunk graph** to reason about. |
| **Split oversized chunks (especially `04`)** | Keeps “one idea per file” and improves **cacheability** of stable prefixes if the provider caches by static prefix. |
| **Conditional sub-chunks** | Include heavy material only when triggers fire (e.g. Blueprint graph work vs asset-only work)—requires **reliable signals** (structured state, not only keywords). |
| **Optional retrieval** | Local vector / snippet retrieval can move **reference** material out of the always-on system string; must **degrade safely** when retrieval is off (already a theme in context-management). |
| **Thread-intent bias** | When user intent is vague early in a thread, **slightly broader inclusion** of tools or context can reduce wrong assumptions; narrow later when signals exist. |

**Tool surface vs prompt chunks:** Narrowing **which tools appear** (packs, dispatch, tiering) is handled in the **tool surface pipeline** and request builder—not in `BuildContextWindow`. Changing chunks does not replace tool-catalog sizing work, and vice versa.

---

## 4.1 Obscure tooling KMin/KMax bumps (working name)

**Problem:** With **dispatch + tiered appendix**, the model only sees **native `tools[]`** as something like **`unreal_ai_dispatch`**; the **real menu** is the **markdown roster** in the system string. Roster size is driven by **guardrail tools** plus **`K` non-guard** tools selected from BM25×**context multipliers**×optional **usage prior**.

**Definitions (repo behavior):**

- **`KMin` / `KMax`:** `UnrealAiRuntimeDefaults::ToolKMin` / `ToolKMax`; fed into `UnrealAiToolKPolicy::ComputeEffectiveK`.
- **Effective `K`:** Chosen from `{KMin, mid, KMax}` based on **margin** between the top two **non-guard** BM25-blended scores (`S0`, `S1`): clear leader → **`KMin`**; muddy middle → **`(KMin+KMax)/2`**; flat leaderboard → **`KMax`**.
- **Guardrail floor:** Tools with `context_selector.always_include_in_core_pack` in **`UnrealAiToolCatalog.json`** are **always** listed first (in global score order among guardrails). **`eligible ≈ (#guardrails) + K`** for the tiered roster. **Raising `K` only adds non-guard tail tools**—it does not replace auditing guardrails.

**“Obscure tooling KMin/KMax bumps”** means: **conditionally raise the floor and/or ceiling** for effective `K` when lexical retrieval is weak or the domain is niche, so **more non-guard tools** stay visible without always paying the cost on crisp queries.

**Candidate signals (not all implemented):**

| Signal | Rough idea |
|--------|------------|
| **Weak BM25 top score** | If **`S0`** is below a threshold, treat the query as **lexically ambiguous** → use **`KMax`** or **mid+** even when margin looks “clear”. |
| **Flat leaderboard** | Small **`(S0 - S1) / S0`** already widens `K` via existing margin buckets; can **steepen** the schedule (e.g. bias toward `KMax` sooner). |
| **Active domain tags** | `UnrealAiToolContextBias::GatherActiveDomainTags` / per-tool tags already scale scores; when **multiple niche tags** fire or **specific** “obscure” tags are active, **bump `KMin`** for that turn so **clear-winner** queries still carry extra tail tools. |
| **Narrow tool pack** | Headed/long-running setups may use **`ToolPackRestrictToCore`** plus **`ToolPackExtraCommaSeparated`**. If the **right tool is not in the candidate pool**, **no `K`** fixes it—**widen the pack** first, then tune `K`. |

**Trade-offs:** Higher `K` increases **system** size (appendix) and **choice overload** (wrong tool picks). Dispatch runs often show **`roster_chars` ~7–8k** vs **`ToolSurfaceBudgetChars` ~80k**, so **budget headroom exists**—the main cost is **behavioral**, not the cap. **`KMin`** is blunter than **`KMax`**: it widens the tail even when BM25 says the top match is strong.

**Natural implementation hooks:** `UnrealAiToolSurfacePipeline::TryBuildTieredToolSurface` (after scoring / tags), or extend `UnrealAiToolKPolicy::ComputeEffectiveK` with optional **`S0` / tag summary** inputs.

---

## 5. Pruning strategy (static instructions)

**Goal:** Remove redundancy and examples that do not change behavior, while keeping **normative** rules the model actually uses.

**Higher-risk areas to trim carefully:**

- Tool-call **shape**, **idempotency**, and **error** semantics
- **Mode boundaries** (Ask vs Agent vs Plan) and what is forbidden in Ask
- **Path semantics** (level vs asset paths, selection vs Content Browser)
- **Safety / permissions** and destructive workflows

**Lower-risk areas (usually):** repeated motivational prose, duplicate lists, long narrative examples that restate the same rule.

**Suggested workflow (benchmark-driven):**

1. Run **one representative harness** (or fixed scenario) on a **known baseline** commit as the accuracy reference.
2. Open a **fresh branch**; **edit existing chunk files in place** (same composition matrix—no structural refactor required).
3. Re-run the **same** test with the **same** model profile and harness options.
4. Compare outcomes (pass/fail, tool errors, classification artifacts, qualitative traces)—not just token counts.

This isolates “prompt length” changes from unrelated code drift.

---

## 6. Saving context (operational knobs)

Distinct levers—often confused:

| Lever | Effect |
|-------|--------|
| **Shorter static chunks** | Smaller **system** message; may improve latency and cost; test for behavior regression. |
| **Context ranker / budgets** | Changes **what** gets into `ContextBlock` from editor state and tool memory—see [`context-management.md`](context-management.md). |
| **History trimming** | Reduces **message list** size when over budget; trades away old turns. |
| **Tool surface narrowing** | Shrinks **`tools` JSON** and/or moves definitions into system appendix (dispatch)—different trade-offs for discoverability vs upload size. |
| **Output limits** | Caps **completion** length; cheap on input bill but can **truncate** reasoning or tool JSON if set too low. |

---

## 7. Pricing intuition (input vs output)

Many providers charge **more per token for output than input**. That does **not** automatically mean “optimize output first”:

- **Input** scales with **every turn** (system + tools + growing history + context).
- **Output** is bounded per turn by task needs; aggressive caps often **reduce reliability** before they save meaningful money.

**Heuristic:** optimize **repeated static input** and **duplicate context** first; tune **output** only when the product genuinely wants shorter answers or you have evidence of wasteful verbosity.

---

## 8. Open questions

- **Measured breakdown:** Persist structured sizes at request-build time (system vs messages vs tools) if we want data-driven prioritization beyond file-size estimates.
- **`05` duplicate placeholder:** Consider consolidating to a single `{{CONTEXT_SERVICE_OUTPUT}}` if we want trace `context_chars` to match **effective** injected size 1:1.
- **Obscure tooling KMin/KMax bumps:** Decide which signals (tags vs `S0` vs pack width) are stable enough to ship; A/B on harness **tool failures** and **extra rounds**, not only `roster_chars`.

---

## 9. Handoff notes for a future agent (discussion context)

Use this section when continuing design threads so you do not re-derive behavior from scratch.

**Artifacts vs semantics**

- **`context_decision_logs/**`** under harness `turns/step_*/`: **context ranker** (snapshots, tabs, tool-result memory, memory snippets). **Not** LLM `tools[]` / dispatch roster decisions.
- **`run.jsonl` `enforcement_event`**: `tool_surface_metrics` (mode, `eligible`, `roster_chars`, `k`, `qshape`, `qhash`, `expanded=` first **`ToolExpandedCount`** ids) and, when enabled, **`tool_selector_ranks`** (full JSON **scores + guardrail flags + selection_reason**). Verbose flag: `UnrealAiRuntimeDefaults::ToolSelectorVerboseLogEnabled`.
- **Latest long-running batches:** `tests/long-running-tests/runs/run_*` and `tests/long-running-tests/last-suite-summary.json` (path rules in `.cursor/rules/testing-long-running-harness.mdc`).

**Code map (high signal)**

- Prompt chunks + order: `UnrealAiPromptBuilder::BuildSystemDeveloperContent` (`Plugins/UnrealAiEditor/.../Prompt/UnrealAiPromptBuilder.cpp`).
- Request merge (system + history + tools + trim): `UnrealAiTurnLlmRequestBuilder::Build` — history trim must **not orphan `role=tool` rows** (helper `TrimApiMessagesForContextBudget`).
- Dispatch roster: `UnrealAiToolSurfacePipeline::TryBuildTieredToolSurface`; **only round 1** of a user send reshapes the surface; repair rounds reuse the prior conversation/tool state.
- Defaults / feature flags: `Plugins/UnrealAiEditor/.../Misc/UnrealAiRuntimeDefaults.h` (harness sync wait, HTTP timeouts, `ToolKMin`/`ToolKMax`, dispatch on/off, tool pack, verbose logs). Not read from `.env`.
- Transport: `FOpenAiCompatibleHttpTransport` logs **`bodyChars` / `toolsChars`** (editor log)—useful for **true** request size, not only context dumps.

**Failure modes to separate from “need more K”**

- Model calls a tool **with bad args** though the tool was in roster → **prompt chunk `04` / examples**, not K.
- Right tool **not in catalog** or **pack** → **catalog + pack**, not K.
- Right tool **BM25-near-zero** for all candidates → **query shaping / tags / retrieval / guardrails**, not only raising `KMax`.

**Pricing / optimization stance (team bias in this thread)**

- Prefer **static instruction pruning** and **duplicate context fixes** before aggressive **output** compression.
- **Obscure tooling KMin/KMax bumps** are a **targeted** alternative to globally raising **`KMin`** (which taxes every “clear winner” query).

---

*This doc is a working narrative for engineering discussion; when behavior changes, update [`context-management.md`](context-management.md) and [`Plugins/UnrealAiEditor/prompts/README.md`](../../Plugins/UnrealAiEditor/prompts/README.md) as the authoritative specs.*
