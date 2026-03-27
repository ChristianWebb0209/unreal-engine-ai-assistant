# Headless Testing Playbook (Agent Handoff)

This document captures how to run and review headless API-backed behavior tests for Unreal AI Editor, with emphasis on realistic vague user prompts and complex multi-turn tasks.

## Goal and philosophy

- Simulate a real end user, not a test engineer.
- Use vague, natural language requests for multi-step editor tasks.
- Evaluate whether the assistant converts intent into valid Unreal tool calls and concrete progress.
- Always inspect the exact context and prompt payload that was sent per turn.

## Four areas of concern (always audit all 4)

1. **Tool definitions**
   - Canonical file: `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`
   - Validate schema compatibility (especially for API function-calling constraints).
   - Ensure summaries and required params are clear enough to route model behavior.

2. **Resolvers / harness runtime behavior**
   - Key files:
     - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/FUnrealAiAgentHarness.cpp`
     - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/UnrealAiTurnLlmRequestBuilder.cpp`
     - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Composer/UnrealAiComposerPromptResolver.cpp`
   - Check whether agent continuation and nudges enforce execution when user asks for concrete actions.

3. **Prompts**
   - Key chunks:
     - `Plugins/UnrealAiEditor/prompts/chunks/04-tool-calling-contract.md`
     - `Plugins/UnrealAiEditor/prompts/chunks/10-mvp-gameplay-and-tooling.md`
     - `Plugins/UnrealAiEditor/prompts/chunks/03-complexity-and-todo-plan.md`
     - `Plugins/UnrealAiEditor/prompts/chunks/01-identity.md`
   - Confirm prompts explicitly require tool calls for action requests (no narration-only completion).

4. **Context management**
   - Service/docs:
     - `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/*`
     - `docs/context-management.md`
   - Verify what context was added, pruned, and why.
   - Confirm context had enough signal at weak turns (so misses are model/runtime policy, not missing data).

## Scripts and commands

## 1) Quick catalog + API sanity

- Script: `tests/tool_catalog_routing_check.py`
- Example:

```powershell
python tests/tool_catalog_routing_check.py --llm --llm-max 12 --audit-log tests/out/live_runs/api_bulk_audit_after_fix_YYYYMMDD.txt
```

Use this for baseline endpoint/tool schema sanity, not deep qualitative behavior.

## 2) Contract consistency check

- Script: `tests/check_tool_contract_consistency.py`
- Example:

```powershell
python tests/check_tool_contract_consistency.py
```

Checks endpoint-safe parameter schema and catalog-vs-dispatch consistency.

## 3) Qualitative vague workflow runner (primary)

- Script: `tests/live_vague_workflow_runner.py`
- Scenarios:
  - `tests/vague_complex_workflow.json`
  - `tests/vague_action_heavy_workflow.json`

Examples:

```powershell
python tests/live_vague_workflow_runner.py --scenario tests/vague_complex_workflow.json --out tests/out/live_runs/vague_complex_rerun_YYYYMMDD.jsonl --sleep-ms 800 --context-max-chars 560
python tests/live_vague_workflow_runner.py --scenario tests/vague_action_heavy_workflow.json --out tests/out/live_runs/vague_action_rerun_YYYYMMDD.jsonl --sleep-ms 800 --context-max-chars 560
```

### Important log fields to inspect

- `request_messages_exact` (full exact payload sent to API)
- `context_before_preview` / `context_after_preview`
- `prune_events_before_turn` / `prune_events_after_turn`
- `tool_calls`
- `tool_call_validations`
- `assistant_content`

## Review process (required)

1. Run one or more vague multi-turn scenarios.
2. Find turns where user asked to **run/start/stop/compile/save/open/fix/apply/change/adjust/tune**.
3. Flag:
   - no tool call returned
   - read-only-only calls on mutation intent
   - status claims that are not backed by matching tool results
4. For each flagged turn, inspect `request_messages_exact` and `context_before_preview`.
5. Decide remediation in one or more of the 4 areas (definitions/resolver/prompts/context).

## Tested topics so far (status)

The following topics were exercised already in live headless runs:

- **Tool schema/API compatibility:** **PASS**
  - Blocking invalid schema issue was fixed (`actor_attach_to` top-level combinator removal).

- **Catalog vs dispatch consistency check:** **PASS** (with expected exclusions for `future`/`experimental` placeholders).

- **Context logging and pruning visibility:** **PASS**
  - Runner logs context growth/pruning reasons and exact request payloads.

- **PIE playtest tool invocation under vague prompts:** **PARTIAL**
  - In some runs, correct `pie_start`/`pie_status` calls appear.
  - In other turns, model still under-executes or delays action despite action intent.

- **Mutation follow-through (read -> write transition):** **PARTIAL / NOT RELIABLE YET**
  - Model sometimes remains in read-only loops for mutation asks (movement/material/compile warning resolution).
  - Prompts improved but did not fully eliminate weak follow-through.

- **Tool argument schema validity of emitted calls:** **PASS (in tested runs)**
  - Emitted calls in reviewed logs were valid against offered schemas.

## Research findings (tool-use reliability literature)

These papers and benchmarks are relevant to the "model does not emit the right tools" problem:

- **Toolformer (NeurIPS 2023)**: tool usage reliability needs explicit learning signal for when/what/how to call, not prompt text alone.
  - https://arxiv.org/abs/2302.04761
- **ReAct (ICLR 2023)**: interleaving reasoning + actions improves grounded execution over narration-only behavior.
  - https://openreview.net/forum?id=WE_vluYUL-X
- **Gorilla + APIBench (2023)**: common failure modes include wrong arguments and API hallucination; retrieval/doc grounding improves call quality.
  - https://arxiv.org/abs/2305.15334
- **BFCL (ICML 2025) + BFCL V3/V4 line**: evaluate multi-turn and multi-step function-calling reliability and stateful correctness, not just syntax.
  - https://proceedings.mlr.press/v267/patil25a.html
- **JSONSchemaBench (2025)**: schema adherence is hard in practice; constrained decoding/strict schema handling matter for production reliability.
  - https://arxiv.org/abs/2501.10868

### Practical implications for this repo

- Prompt-only fixes are usually not sufficient.
- Reliability improves most when combining:
  1) strict schema/tool surface constraints,
  2) clearer tool definitions,
  3) runtime enforcement in resolver/harness loops,
  4) realistic multi-turn vague-prompt evaluation.

## Plan of approach (next iterations)

Use this phased plan for future agents. Each phase maps to one or more of the 4 concern areas.

### Phase 1 — Establish strict baseline contracts

- **Tool definitions**
  - Keep endpoint-compatible parameter schemas.
  - Expand per-tool summaries with stronger "when to use / when not to use".
- **Resolvers**
  - Ensure no assistant-only stop on action-intent turns without either:
    - at least one action tool call, or
    - explicit blocker explanation.

Exit criteria:
- No schema-level API rejections in baseline runs.
- No silent completion on clearly action-oriented user turns.

### Phase 2 — Strengthen action-intent execution

- **Prompts**
  - Keep strict action-turn and mutation follow-through rules.
  - Add examples for common weak patterns (playtest, compile-fix, material tuning).
- **Resolvers**
  - Add bounded corrective nudges for:
    - action intent + no tool calls,
    - mutation intent + read-only-only tool sequence.

Exit criteria:
- Significant reduction of "NO_TOOL action turn" and "READ_ONLY_ON_MUTATION" flags across vague scenarios.

### Phase 3 — Improve disambiguation and routing precision

- **Tool definitions**
  - Tighten overlapping tool descriptions (for example read-only inspectors vs mutators).
  - Prefer canonical keys and include minimal valid argument examples.
- **Context management**
  - Ensure context keeps actionable targets (selected assets, resolved object paths, recent successful lookups).
  - Confirm pruning does not drop critical target paths too early.

Exit criteria:
- Fewer redundant discovery loops when target is already known.
- Faster progression from discovery to mutation on multi-turn tasks.

### Phase 4 — Expand scenario coverage by domain

Future agents should test new topic families beyond current movement/material/PIE focus:

- actors/world transforms and placement workflows
- source/code search + project-file edits
- asset pipeline flows (create/rename/save/dependency updates)
- build/packaging-related requests
- UI/navigation-heavy tasks
- mixed multi-system tasks (Blueprint + materials + scene + PIE)

Suggested scenario seeds in this repo:

- `tests/vague_actors_world_workflow.json`
- `tests/vague_source_project_edit_workflow.json`
- `tests/vague_asset_pipeline_workflow.json`
- `tests/vague_build_packaging_workflow.json`
- `tests/vague_ui_navigation_workflow.json`
- `tests/vague_mixed_multisystem_workflow.json`

Exit criteria:
- Topic-level matrix where each domain has:
  - at least one vague complex scenario,
  - qualitative review notes,
  - pass/partial/fail status and remediation map.

## Guidance for future agents

- Use new vague scenarios focused on uncovered domains (for example actors/world transforms, source/code edits, build/packaging flows, UI navigation-heavy tasks, large multi-asset refactors).
- Keep scenarios realistic: imperfect user memory, mixed goals, and changing priorities.
- Do not rely on pass/fail only; perform qualitative review per turn with exact context/prompt evidence.
- When proposing fixes, explicitly map each fix to one of the 4 concern areas.
