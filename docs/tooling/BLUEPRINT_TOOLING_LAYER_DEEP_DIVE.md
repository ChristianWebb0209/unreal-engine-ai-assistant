# Blueprint Tooling Layer Deep Dive

This document explains how we intentionally built the Unreal AI Editor tooling layer to be highly reliable for Blueprint creation, even when that meant more LLM round trips and higher latency. The core trade-off is simple: we chose **specialization and strictness over speed**.

---

## Why we moved beyond one agent + one tool catalog

A common competitor pattern is a single agent persona with one broad tool catalog for every workflow. That approach can be fast, but it repeatedly underperforms for Blueprint-heavy work because:

- The model has to choose from too many tools at once.
- Similar-but-different Unreal graph formats get conflated.
- Blueprint graph references (GUIDs, pin names, op ordering) are easy to hallucinate.
- Generic prompts do not encode format edge cases deeply enough.

In our system, we observed that the default single-surface approach led to avoidable errors such as:

- calling builder-only mutators from the main turn,
- generating invalid patch op shapes,
- inventing semantic values outside allowed enums,
- producing plausible-but-invalid references (placeholder IDs, wrong class paths, wrong pin direction).

So we moved to a **multi-surface architecture** where each task class has a tighter prompt stack and a narrower tool surface.

---

## The architecture we shipped

At runtime, we use distinct lanes instead of one universal lane:

1. **Main Agent / Orchestrator lane**  
   Handles user intent, decomposition, discovery, and delegation.

2. **Blueprint Builder sub-turn**  
   Handles deterministic Blueprint mutations and verification loops.

3. **Product specialist sub-turns**  
   Optional domain experts (animation, materials, diagnostics, etc.) with specialist-specific tool allow-lists.

This is represented in code and prompt assembly, not just documentation prose:

- Tool catalog merges fragments (`tools.main.json` + `tools.blueprint.json`) and carries retrieval metadata.
- Agent-surface gating (`agent_surfaces`) decides whether a tool is eligible on the current lane.
- Prompt assembly loads different chunk stacks for main vs orchestrator vs specialist vs blueprint-builder turns.
- The harness explicitly transitions between lanes via protocol tags.

---

## Core design principle: constrain the model by task format

Blueprint creation is not one format. It is a bundle of partially overlapping formats with strict constraints. We treat each format as an interface contract and place it in the lane where the model can best succeed.

### Format 1: Delegation tag protocol

Main agent delegates Blueprint work via:

- `<unreal_ai_build_blueprint> ... </unreal_ai_build_blueprint>`
- YAML frontmatter with `target_kind`.

Supported `target_kind` values include:

- `script_blueprint`
- `anim_blueprint`
- `material_instance`
- `material_graph`
- `niagara`
- `widget_blueprint`

Why it matters: this tag is the bridge from broad intent to a format-specific builder stack. It is the first anti-hallucination checkpoint.

### Format 2: Builder result protocol

Builder returns control with:

- `<unreal_ai_blueprint_builder_result> ... </unreal_ai_blueprint_builder_result>`
- structured status (`success | partial | blocked`) and handoff notes.

Why it matters: sub-turn boundaries are explicit and machine-parseable, so the orchestrator can resume with clean state.

### Format 3: Tool catalog JSON + surface metadata

`tools.main.json` holds global metadata and tool schemas, then merges fragments:

- `tools.blueprint.json` (blueprint-oriented tools)

Critical catalog controls:

- `tool_catalog_fragments` with `retrieval_bundle` tags,
- per-tool `agent_surfaces` (`main_agent`, `blueprint_builder`, `blueprint_builder`, `all`),
- strict `additionalProperties: false` schemas,
- mode flags (`ask/agent/plan`).

Why it matters: the catalog is no longer just a function list; it is a routing and validation contract.

### Format 4: Dispatch tool-call payloads

Dispatch uses strict JSON argument schemas and resolver validation before execution:

- required fields are enforced,
- unknown fields are rejected for strict schemas,
- canonicalization repairs known aliases,
- invalid calls return structured `validation_failed` payloads with `suggested_correct_call`.

Why it matters: most hallucinations are stopped before they mutate assets.

### Format 5: Blueprint graph patch op format

`blueprint_graph_patch` is an atomic, ordered op batch with explicit op shapes such as:

- `create_node`
- `create_comment`
- `connect` / `connect_exec`
- `break_link`
- `splice_on_link`
- `set_pin_default`
- `add_variable`
- `remove_node`
- `move_node`

Important constraints include:

- all-or-nothing transaction (`applied_partial` remains empty on failure),
- strict op schema with `oneOf` variants,
- closed-set `semantic_kind` values,
- explicit node reference grammar (batch `patch_id`, `guid:...`, or bare UUID where supported),
- optional `validate_only` dry runs.

Why it matters: this format encodes graph surgery precisely enough for deterministic retries.

### Format 6: Graph introspection and verification formats

Read/verify tools provide machine-grounded graph state:

- `blueprint_graph_introspect` for nodes/pins/links,
- `blueprint_graph_list_pins` for exact pin names/directions/categories,
- `blueprint_verify_graph` and `blueprint_compile` for post-mutation validation.

Why it matters: “discover first, then mutate” replaces guesswork with live graph facts.

### Format 7: Prompt chunk format (composable domain instructions)

Prompt assembly is chunked and mode-aware:

- shared common chunks,
- orchestrator-only chunks,
- specialist-specific chunks,
- blueprint-builder chunks,
- environment-builder chunks.

Blueprint-builder chunks include canonical guidance for:

- deterministic loops,
- cross-tool identity handling,
- graph patch canonical recipes,
- known failure and handoff behaviors.

Why it matters: format-specific instructions are loaded only when relevant, increasing signal density and reducing ambiguity.

### Format 8: Harness telemetry / run artifacts

`run.jsonl` artifacts capture per-turn tool events, failures, and summaries. This powers iterative reliability work (curriculum runs, failure triage, prompt/catalog/dispatch fixes).

Why it matters: reliability improvements are measured from real turn traces, not intuition.

---

## How tool surfaces increase accuracy

The main accuracy gain comes from reducing “decision entropy” per turn.

### Main lane behavior

When main-agent blueprint mutation tools are intentionally omitted, the main lane focuses on:

- discovery,
- planning,
- delegation.

If main tries builder-only mutators, the gate blocks them (surface-withheld behavior), forcing the correct handoff path.

### Blueprint Builder behavior

Once inside Blueprint Builder:

- tool appendix is domain-filtered for `target_kind`,
- mutation/verify tools are available and documented with deeper context,
- canonical patch rules and anti-hallucination constraints are active.

Result: more calls, but significantly fewer malformed mutation attempts.

### Orchestrator and specialist behavior

Orchestrator and specialist lanes use fixed allow-list policies rather than broad retrieval-ranked catalogs. This keeps these turns focused on delegation and domain-scoped execution.

### Round behavior

For default wide-agent selection, BM25 tiering is primarily a round-1 tool-surface optimization; follow-up rounds keep continuity. Orchestrator/specialist allow-lists are rebuilt each round to remain aligned with policy gates.

---

## Hallucination handling: explicit, layered, and practical

Because Unreal Blueprint graph formats are less dominant in model pretraining data than mainstream web/backend schemas, hallucination handling is treated as a first-class system feature.

We mitigate at multiple layers:

1. **Prompt contracts**  
   Canonical recipes, closed-set values, and “do not invent” rules.

2. **Catalog schemas**  
   Strict argument validation (`additionalProperties: false`, enum restrictions, required keys).

3. **Resolver canonicalization**  
   Alias normalization and structured error payloads with concrete corrective suggestions.

4. **Dispatch semantics**  
   Atomic patch transactions, validate-only simulation, and explicit diagnostics.

5. **Hallucination normalizers**  
   Known pseudo-op rewrites for frequent model mistakes (for legacy IR paths and similar fallback contexts).

6. **Harness-driven iteration**  
   Curriculum suites repeatedly expose recurring failure modes; fixes are applied in prompts/catalog/dispatch/tests and re-measured.

This layering is exactly why we accepted extra latency: each layer removes a failure class that a single-agent/single-catalog setup typically leaves unresolved.

---

## Why this was necessary for Blueprint-specific work

Blueprint creation tasks combine:

- graph topology edits,
- typed pin compatibility,
- engine-specific class/function pathing,
- compile and verification feedback loops,
- editor-state-sensitive operations.

These constraints are strict and often underrepresented in generalized LLM priors. The model needs **highly local, task-specific context** to stay correct:

- which tool family is valid in this turn,
- which references are legal,
- which values are closed enums,
- which graph domains are unsupported or partial (for example, AnimGraph/state-machine edits vs K2-compatible script graphs).

A single broad prompt/tool surface cannot consistently provide this precision without overwhelming the model with irrelevant options.

---

## Trade-off analysis: more round trips, better outcomes

Yes, the architecture increases:

- handoff boundaries,
- LLM turns,
- token usage,
- wall-clock latency.

But it decreases:

- invalid tool calls,
- schema mismatch loops,
- cross-domain tool confusion,
- unsafe or non-actionable hallucinations,
- wasted retries on impossible operations.

For Blueprint-heavy product quality, this is the correct optimization target: **reliable completion and correctness over lowest-latency first response**.

---

## What this means compared with single-catalog competitors

Compared with one-agent/one-catalog systems, our implementation is intentionally opinionated:

- more role-specialized agent surfaces,
- more format-specific prompt stacks,
- stronger pre-dispatch validation,
- explicit protocol markers for cross-lane handoff,
- domain-aware tool filtering per turn.

Competitor systems often prioritize a flat universal interface. We prioritize correctness under complex editor graph constraints.

---

## Practical lessons from building this

1. **Format-specific context beats generic “smartness.”**  
   The biggest gains came from narrowing context and tools to exactly what the turn needed.

2. **Strict schemas are a feature, not friction.**  
   Early rejection plus good corrective payloads is cheaper than silent bad mutations.

3. **Protocolized delegation scales better than ad hoc prompting.**  
   Typed handoff blocks make the harness deterministic.

4. **Atomic mutation semantics are essential.**  
   Blueprint graph updates must fail safely and visibly.

5. **Reliability requires instrumentation loops.**  
   `run.jsonl` plus repeatable qualitative curricula made failures actionable.

---

## Bottom line

We did not build a single “universal Blueprint agent.” We built a **layered tooling system with multiple agent types and multiple tool catalogs/surfaces**, because Blueprint workflows are format-sensitive and unusually prone to LLM hallucination when context is too broad.

The result is a slower but substantially more accurate system: each turn carries the smallest correct tool universe and the richest relevant instructions for that specific format, which is exactly what Blueprint creation reliability demanded.

---

## Interview response prep: 3 detailed answers

Below are ready-to-use responses you can adapt in an interview. They are written to sound specific, technical, and reflective, while staying understandable to non-specialists.

### 1) A specific task I redesigned using AI, and how I prompted it

One concrete task I redesigned was **Blueprint graph creation and editing** in Unreal. Initially, the AI setup looked like many competitors: one agent with one broad tool list. It was quick, but accuracy was inconsistent. The model frequently mixed up tool contexts, invented arguments, or attempted graph mutations in the wrong phase.

I redesigned that workflow into a **multi-lane system**:

- A main/orchestrator lane handles intent, discovery, and delegation.
- A Blueprint Builder lane handles deterministic graph mutation and compile/verify loops.
- Tool access is lane-specific using `agent_surfaces`, so the model cannot call mutation tools from the wrong lane.

Prompting strategy was the main lever. Instead of one generic prompt, I used **structured, format-aware prompts**:

1. **Main-turn prompting for delegation**
   - Tell the model to gather facts first (asset paths, graph context).
   - Require explicit handoff to builder mode with:
     - `<unreal_ai_build_blueprint>`
     - YAML `target_kind` (for example `script_blueprint`, `anim_blueprint`, `material_graph`)
   - Avoid freeform "just do it" instructions in the main lane.

2. **Builder-turn prompting for deterministic execution**
   - Force a canonical loop: introspect graph -> patch -> compile/verify.
   - Require legal node references only (`patch_id`, GUID forms), never guessed placeholders.
   - Emphasize closed enums and schema-compliant op shapes.
   - Include known edge cases like exec-vs-data pin wiring and strict op ordering.

3. **Validation-first prompting**
   - For risky changes, ask for `validate_only` style dry runs before commit.
   - If validation fails, prompt for correction using returned diagnostics, not intuition.

The outcome was slower per request due to handoffs and extra tool calls, but significantly more reliable. The redesigned flow reduced invalid mutations and made failures explainable and recoverable.

### 2) A time I caught an AI hallucination or biased answer, and how I adjusted

A recurring hallucination I caught was the model producing **plausible but invalid Blueprint patch calls**. It would generate semantically reasonable actions, but with wrong details:

- unsupported `semantic_kind` values,
- non-existent pin names,
- guessed node references,
- function/class pairings that looked right but were wrong for Blueprint context.

The key signal was that outputs looked fluent but failed deterministic checks. I shifted from trusting fluent answers to trusting **machine-verifiable evidence**. My adjustment had three parts:

1. **Ground every mutation in discovery**
   - Require `blueprint_graph_introspect` / pin listing before patching.
   - Treat guessed references as a hard failure condition.

2. **Add strict guardrails in tooling**
   - Keep `additionalProperties: false` and required fields in schemas.
   - Return structured `validation_failed` with `suggested_correct_call`.
   - Enforce atomic patch behavior so partial bad edits never persist.

3. **Teach known failure patterns explicitly**
   - Add prompt guidance on common hallucinations and format intricacies.
   - Add normalization/repair only for known recurring patterns, not broad magic autocorrect.

That changed my operating model: AI is a strong generation partner, but correctness comes from a loop of constrained generation + strict validation + explicit recovery.

If you want a concise interview soundbite for this:  
"I stopped evaluating AI by fluency and started evaluating it by contract compliance. Once we did that, reliability went up fast."

### 3) An AI-driven improvement idea in my field, and human-centric risks I would watch

A high-impact idea in this field is an **AI Blueprint Reliability Copilot** that works as a continuous quality layer, not just a code generator. Instead of only generating graph edits, it would:

- preflight-check proposed changes against project conventions and schema constraints,
- run simulation/validation passes before apply,
- explain risks in plain language,
- offer safe auto-fixes with explicit user approval thresholds.

In practice, this becomes a "design + verification copilot" for technical artists, gameplay engineers, and designers using Unreal editor tooling.

The human-centric risks I would watch closely:

1. **Over-automation risk**
   - Teams may stop understanding core Blueprint mechanics.
   - Mitigation: enforce explain-why summaries and learning breadcrumbs per major change.

2. **Trust calibration risk**
   - Users may over-trust fluent outputs or under-trust useful automation.
   - Mitigation: show confidence and evidence (what was introspected, what was validated, what failed).

3. **Workflow exclusion risk**
   - Tooling may privilege expert users and leave less technical users behind.
   - Mitigation: progressive disclosure UX (simple mode, advanced mode, transparent diagnostics).

4. **Bias in recommendations**
   - AI may over-recommend familiar patterns and under-represent novel design choices.
   - Mitigation: present alternatives and trade-offs, not single-path prescriptions.

5. **Accountability risk**
   - Ambiguity over who "owns" a bad automated change.
   - Mitigation: auditable change traces, explicit approval points, rollback-safe defaults.

How I would frame this in an interview:  
"The opportunity is not just AI that writes Blueprints. It is AI that helps teams make safer, more understandable Blueprint decisions. The product win is paired with a human-factors responsibility: preserve agency, understanding, and accountability."
