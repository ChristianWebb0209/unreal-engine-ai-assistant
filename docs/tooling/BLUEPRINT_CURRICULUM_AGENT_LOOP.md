# Blueprint curriculum: headed harness loop for agents

This document is for an **autonomous maintainer agent** (or human) that improves Blueprint toolcalling reliability by running a fixed multi-turn suite, analyzing failures, fixing prompts/tooling, graduating easy turns, and replacing them from a backlog so the suite always stays at **10 turns**.

**Suite file:** [`tests/qualitative-tests/suites/blueprint-creation-curriculum-v1.json`](../../tests/qualitative-tests/suites/blueprint-creation-curriculum-v1.json)

**Runner:** from repo root,

```powershell
.\tests\qualitative-tests\run-qualitative-headed.ps1 -Suite blueprint-creation-curriculum-v1
```

**Broader harness context:** [`AGENT_HARNESS_HANDOFF.md`](AGENT_HARNESS_HANDOFF.md)

**Architecture note:** headed curricula exercise the **full** tool path. On the **default main Agent**, Blueprint **graph mutators** are often **not** on the roster (`agent_surfaces` + `bOmitMainAgentBlueprintMutationTools`); the model is expected to **hand off** via **`<unreal_ai_build_blueprint>`** for patch/IR/compile loops. Failures that look like “model tried `blueprint_graph_patch` but tool was missing” often mean prompt/catalog drift—align with [`prompts/chunks/04-tool-calling-contract.md`](../../Plugins/UnrealAiEditor/prompts/chunks/04-tool-calling-contract.md) and [`12-build-blueprint-delegation.md`](../../Plugins/UnrealAiEditor/prompts/chunks/12-build-blueprint-delegation.md).

---

## Goal

Increase the **end-to-end pass rate** of the 10-turn curriculum (real LLM + real editor + real tools), measured by:

- Each turn’s `turns/step_XX/run.jsonl` ending with `type: run_finished` and `success: true`.
- Fewer `tool_finish` rows with `success: false` for **avoidable** reasons (schema confusion, bad refs, missing catalog hints).

Do **not** “improve” metrics by weakening turn requirements or skipping tools.

---

## One iteration cycle (repeat for hours)

0. **Preflight (no LLM):** After C++ or automation changes, run `.\build-editor.ps1 -AutomationTests -Headless` from repo root so `UnrealAiEditor.Tools.BlueprintPinTypeParse` and other dispatch contract tests pass before you spend API time on headed runs.

1. **Run the suite** (headed; API keys in Unreal AI Editor settings as for any qualitative batch).
2. **Locate artifacts:** under `tests/qualitative-tests/runs/run-<timestamp>-blueprint-creation-curriculum-v1/` (exact folder name varies by date/time).
3. **Per turn** (`step_01` … `step_10`):
   - Open `turns/step_XX/run.jsonl`.
   - Confirm last line is `run_finished`. If `success` is false, record `error_message`.
   - Scan for `type: tool_finish` with `success: false`; note `tool` id and error payload text.
   - Note the **last few tools** invoked before failure (ordering bugs often show up here).
4. **Batch classify:**

   ```bash
   python tests/classify_harness_run_jsonl.py --from-summary tests/qualitative-tests/runs/<batch-folder>/last-suite-summary.json
   ```

   Or pass `--batch-root` to the run folder per the script’s help.

5. **Triage** each failure into **one owner** (pick the single best fix surface):

   | Symptom | Likely owner |
   |---------|----------------|
   | Model skips tools / wrong tool order | `Plugins/UnrealAiEditor/prompts/chunks/*.md`, especially `04-tool-calling-contract.md` |
   | Invalid JSON args / schema mismatch | `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json` |
   | Tool returns misleading or strict errors | `Plugins/UnrealAiEditor/Source/.../Tools/UnrealAiToolDispatch*.cpp` |
   | Repeatable without LLM | Add **`WITH_DEV_AUTOMATION_TESTS`** case in [`UnrealAiToolDispatchAutomationTests.cpp`](../../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatchAutomationTests.cpp) or domain tests |
   | Rate limits / timeouts | Model profile / throttling; not a prompt fix |

6. **Fix:** Prefer **small, test-backed** changes: clearer catalog `summary`/schema, stricter or more lenient dispatch where intentional, prompt bullets that match actual tool names. After dispatch fixes, run:

   ```powershell
   .\build-editor.ps1 -AutomationTests -Headless
   ```

7. **Re-run** the full headed suite and compare step pass/fail.

---

## Graduate easy turns (keep N = 10)

**Policy:** If turn *i* passes **K** consecutive full suite runs (recommended **K = 3**) with no manual caveat (e.g. “model got lucky once”), **remove** turn *i* from [`blueprint-creation-curriculum-v1.json`](../../tests/qualitative-tests/suites/blueprint-creation-curriculum-v1.json) and **append** one new turn from the **Backlog** section below (or a new item you document in the same backlog order).

- Edit the suite JSON only: delete one `turns[]` object, append one new object.
- Update `coverage_notes` if the thematic focus shifts.
- Do not drop below **10** turns.

---

## Optional mechanical checks

After tool sequences stabilize for a turn, you can gate locally:

```bash
python tests/assert_harness_run.py path/to/turns/step_04/run.jsonl --expect-tool blueprint_graph_patch --expect-tool blueprint_compile --require-success
```

Repeat `--expect-tool` as needed. This is **optional** for headed quality runs; use it when you promote a scenario to CI-like scrutiny.

---

## Guardrails

- If a backlog turn is too hard for current tools, **lower difficulty inside the backlog** (add a headless test + smaller prompt in dispatch first), then re-add the harder variant later.
- Keep assets under **`/Game/UnrealAiHarness/BpCurriculum`** so runs do not trash arbitrary project content.
- Follow **`04-tool-calling-contract.md`**: discovery before invented paths; no `{}` on tools with required fields.

---

## Turn backlog (replacement candidates)

Use these when graduating a turn. Remove the used item from this list in the same PR (or move to a “done” subsection) so the next agent does not duplicate.

1. Add a **Function** graph (not only EventGraph): new function, call it from BeginPlay via `blueprint_graph_patch`.
2. **Macro** graph: define a macro with an input pin; invoke from ubergraph.
3. **Interface**: Blueprint implements `BPI_*`; call interface message on self.
4. **Enhanced Input**: bind an IA to an action; document dependency on project having Enhanced Input assets or create minimal IA/IMC under `/Game`.
5. **Data-only** or **Blueprint Interface** asset under `/Game/UnrealAiHarness/BpCurriculum`.
6. **Duplicate** an existing curriculum BP with `asset` tools or editor flow; verify compile.
7. **Rename** a curriculum asset with `asset_rename`; fix up references; compile dependents.
8. **Timer** or **Delay** latent chain in ubergraph (IR or patch); compile.
9. **Multicast** delegate custom event / dispatch (if tooling supports export/patch cleanly).
10. **Component** add: `Add Component by Class` in construction or BeginPlay; compile.
11. **Cast** chain: `K2Node_DynamicCast` from Actor to a project subclass; null branch.
12. **Struct** variable: add struct member variable; break/make in graph.
13. **Enum** variable: switch on enum in graph.
14. **Array**: add array variable; get length / for-each (if op set allows).
15. **Multi-graph compile**: touch Function + EventGraph; `blueprint_compile` once.
16. **`merge_policy` stress**: `blueprint_apply_ir` `append_to_existing` with second BeginPlay fragment; verify merge_warnings.
17. **Comment box**: `blueprint_graph_patch` `create_comment` with `member_node_refs`.
18. **`blueprint_set_component_default`**: tune a component default on an Actor BP in curriculum folder.
19. **PIE smoke**: `pie_start` after successful compile (short; may be environment-sensitive).
20. **Retrieval-assisted** edit: run codegen/embedding tool if catalog includes it, then patch BP using retrieved path.

---

## Files to touch in a typical fix

| Layer | Path |
|--------|------|
| Prompts | `Plugins/UnrealAiEditor/prompts/chunks/` |
| Catalog | `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json` |
| Dispatch | `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatch*.cpp` |
| Headless tests | `Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tools/UnrealAiToolDispatchAutomationTests.cpp` |
| This suite | `tests/qualitative-tests/suites/blueprint-creation-curriculum-v1.json` |
