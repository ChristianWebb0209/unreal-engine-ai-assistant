# Blueprint graph patching: reliability gaps vs reference projects

This note explains why `blueprint_graph_patch` is easy for agents to misuse (using the triple-jump / `BP_FirstPersonCharacter` failure as a concrete example), how five projects under `reference/` approach Blueprint editing differently, and what we should implement to move error surface from the model into the engine.

Evidence base: shipped code in `Plugins/UnrealAiEditor` (especially `UnrealAiToolDispatch_BlueprintGraphPatch.cpp`), prompts in `Plugins/UnrealAiEditor/prompts/chunks/blueprint-builder/`, the cross-plugin write-up in `reference/arch-analysis.md`, and readable sources in `reference/ue-llm-toolkit` and `reference/autonomix`.

---

## Symptom: triple-jump patch loops

A typical failure chain:

1. The agent must emit **`create_node`** ops with a valid **`k2_class`** (a loadable `UClass` path for a **`UK2Node`** subclass), then wire with **`connect`** using real **`guid:…`** or batch-local **`patch_id`** refs.
2. Models often invent **non-existent node classes** (e.g. treating `K2Node_CallFunction` as if it were a math node class, or requesting `K2Node_IntLess` / `K2Node_Sequence` style names that do not exist or are not the real Unreal class).
3. Integer math and compares in Kismet are normally **`UK2Node_CallFunction`** nodes bound to **`UKismetMathLibrary`** (`/Script/Engine.KismetMathLibrary`) with function names like `Less_IntInt`, `Add_IntInt` — documented in `Plugins/UnrealAiEditor/prompts/chunks/blueprint-builder/07-graph-patch-canonical.md`, but still easy to get wrong under pressure.
4. **`blueprint_graph_patch` is atomic**: any invalid op aborts the **entire** transaction (`applied_partial` is always empty on failure). One bad `create_node` at the start prevents even a correct `add_variable` from persisting, which encourages **identical retries** and harness “repeated tool failures” shutdowns.

So the problem is not only “bad prompts”; it is **API shape + atomicity + full Unreal node vocabulary** exposed directly to the LLM.

---

## What is wrong with our system (technical root causes)

### 1. Low-level `k2_class` is a huge search space

`CreateNodeFromPatchOp` loads **`k2_class`** with `LoadObject<UClass>` and requires `IsChildOf(UK2Node::StaticClass())` (`UnrealAiToolDispatch_BlueprintGraphPatch.cpp`). The model must know:

- Exact **`/Script/BlueprintGraph.K2Node_*`** (or InputBlueprintNodes for Enhanced Input),
- That **math/compare “nodes” are not dedicated `K2Node_*` types** but **`K2Node_CallFunction`** + library function,
- Correct **`event_override`** shapes for overrides (`Landed` + **`/Script/Engine.Character`**, etc.).

Resolver repairs (see `UnrealAiToolCatalog.json` / `UnrealAiToolDispatch_ArgRepair.cpp`) fix *some* aliasing, but they cannot fix **semantically wrong class choices**.

### 2. Confusing or wrong class names are a recurring failure mode

Examples that show up in real agent runs:

- **`K2Node_Sequence`**: Unreal’s multi-exec fan-out node is typically **`UK2Node_ExecutionSequence`** (`/Script/BlueprintGraph.K2Node_ExecutionSequence`), not a class called `K2Node_Sequence`. Guessing the string “Sequence” does not make `LoadObject` succeed.
- **`K2Node_CallFunction` with wrong `class_path`**: the **node** class is always `K2Node_CallFunction`; the **target** of the call is `class_path` + `function_name`. Mixing these up produces invalid graphs or early errors.

### 3. Wiring identity is correct but heavy

Patch **`connect`** requires **output → input** direction and stable node refs (`guid:…` from export/introspect, or `patch_id` for nodes created in the same batch). This is **faithful to the engine** but **cognitively expensive** for models compared to “connect node A pin X to node B pin Y” after the tool returns **stable server-side IDs**.

### 4. Atomic all-or-nothing batches amplify mistakes

Atomicity is good for asset integrity but **bad for iterative correction** when the first op is invalid: nothing lands, the agent may repeat the same JSON, and the harness stops after N identical failures.

### 5. Multiple authoring paths without a forced decision tree

We already support **`blueprint_apply_ir`**, **`blueprint_graph_patch`**, and **T3D** (`blueprint_export_graph_t3d`, `blueprint_t3d_preflight_validate`, `blueprint_graph_import_t3d`). The docs describe when to prefer each (`07-graph-patch-canonical.md`, `01-deterministic-loop.md`). In practice, agents still default to **large `blueprint_graph_patch`** for tasks that Autonomix-style **T3D injection** or higher-level **IR** would handle with less room for class-path error.

---

## The five `reference/` projects (and what they do instead)

The five competitors called out in `reference/arch-analysis.md`:

| Project | Blueprint edit strategy | Determinism lever |
|--------|-------------------------|-------------------|
| **Autonomix** (`reference/autonomix`) | **T3D injection** (`inject_blueprint_nodes_t3d`) + placeholder GUID resolution + `ImportNodesFromText` | Model edits **text**; engine resolves placeholders; **pre-flight validation** against reflection; **verify** ladder after compile |
| **UE LLM Toolkit** (`reference/ue-llm-toolkit`) | **`blueprint_modify`** with **`node_type`** + **`node_params`** | **Closed vocabulary** of supported types; native code maps to `FBlueprintGraphEditor::CreateNode` (`CallFunction`, `Branch`, `Sequence`, `VariableGet`, …) — see `BlueprintGraphEditor.cpp` |
| **Ludus** (`reference/ludus`, docs only in tree) | Chat-driven spawn/connect in **open** graphs | UX + context modes; **no** from-scratch `.uasset` creation — narrows the problem |
| **LLM Sandbox** (`reference/llm-sandbox`) | General Python / scene tooling | **Not** a Blueprint-specific deterministic graph contract in the snapshot |
| **Aura** (`reference/Aura`) | Autocomplete / assistive patterns; binaries elsewhere | **Not** an agent-scale “patch language” in accessible sources |

**Nwiro** under `reference/` is PCG / environment oriented (`nwiro-docs.txt`); it is **not** a comparator for K2 graph patch APIs.

**Takeaway:** the two strongest patterns for **reducing agent error** are (a) **semantic, server-defined node kinds** (Toolkit) and (b) **bulk text graph authoring with placeholders + validation** (Autonomix). Our `blueprint_graph_patch` is closer to a **raw assembly** API — maximum power, maximum ways to fail.

---

## What we should learn (design principles)

1. **Never require the model to know arbitrary `UClass` paths** for common workflows. Either map a **small enum** of intents to C++ creation code (Toolkit), or move bulk work to **T3D** (Autonomix).
2. **Stable node handles across tool calls** matter. Toolkit writes deterministic IDs into node metadata (`NodeComment`) and documents that subsequent **`connect_pins`** use those IDs (`reference/.../Resources/domains/blueprints.md`). Our batch-local `patch_id` is good **inside one call** but does not replace a **cross-call** story unless we export GUIDs and force the model to round-trip them (high friction).
3. **Validate before mutate** where possible: Autonomix-style preflight breaks retry loops that burn identical atomic transactions.
4. **Composite ops** beat ten fragile `create_node` lines for patterns like “int compare”, “increment int variable”, “branch on bool”, “sequence N outputs”.
5. **Choose one primary path per task class** in prompts *and* harness policy: e.g. “subgraph / new feature block → T3D; single edge splice → `splice_on_link` + minimal nodes; tabular logic → IR if op exists”.

---

## Implementation steps (recommended order)

### Phase A — Reduce failure rate without new graph semantics

1. **Expand resolver / repair for common mistakes** in `UnrealAiToolDispatch_ArgRepair.cpp` (and tests in `UnrealAiToolDispatchAutomationTests.cpp`): e.g. map alias strings like `Sequence` / `ExecutionSequence` → `/Script/BlueprintGraph.K2Node_ExecutionSequence`; reject or rewrite known-bad `k2_class` patterns before `LoadObject`.
2. **Enrich `suggested_correct_call`** in patch errors for `invalid_k2_class` with **copy-paste examples** for `KismetMathLibrary` (`Less_IntInt`, `Add_IntInt`) and execution sequence nodes (exact `k2_class` path).
3. **Optional dry-run mode** (new tool or flag): parse and validate **all** ops (load classes, resolve refs against exported GUID set, pin direction checks) **without** starting the transaction, returning **per-op indices** so the model fixes one row at a time.

### Phase B — Semantic node creation (Toolkit-style) inside our stack

4. Add **`create_node_semantic`** (name TBD) or extend `create_node` with **`kind`** / **`template`** when `k2_class` is omitted: e.g. `call_library_function`, `branch`, `execution_sequence`, `variable_get`, `variable_set`, `event_override`. Implementation delegates to the same underlying `NewObject<UK2Node>` + `SetFromFunction` paths we already use, but **only** after C++ validates parameters.
5. Document a **closed vocabulary** in `UnrealAiToolCatalog.json` and `07-graph-patch-canonical.md` so the schema itself steers agents away from raw `k2_class` except for **escape hatch** nodes.

### Phase C — Autonomix-style reliability layers

6. **Promote T3D + `blueprint_t3d_preflight_validate` + `blueprint_graph_import_t3d`** in the Blueprint Builder system prompt as the **default** for “add a coherent subgraph” tasks; keep `blueprint_graph_patch` for **surgical** edits (`splice_on_link`, one branch, one extra call).
7. Add a **`blueprint_verify_*` ladder** (as sketched in `reference/arch-analysis.md` suggestions): structured post-apply audit (broken exec chains, dangerous defaults) with machine-readable codes, not only compile success.

### Phase D — IR and harness behavior

8. **Grow `blueprint_apply_ir`** op coverage for recurring gameplay patterns (e.g. counter + cap + reset-on-event) so fewer tasks need arbitrary K2.
9. **Harness:** when `blueprint_graph_patch` fails with the same `error_codes` twice, **force** a tool switch (e.g. require `blueprint_graph_introspect` + smaller patch, or T3D path) instead of allowing identical JSON loops.

---

## Bottom line

`blueprint_graph_patch` fails agents because it is **honest low-level Unreal** in a **single atomic transaction**: the LLM must act like a Blueprint compiler frontend. **UE LLM Toolkit** hides that behind **`node_type`**. **Autonomix** hides it behind **T3D + placeholders + validation**. We already have the pieces (IR, T3D import, formatter, introspect); the product gap is **policy + semantic sugar + validation layering** so deterministic behavior does not depend on the model guessing `k2_class` strings correctly.

For maintenance, keep `reference/arch-analysis.md` as the broader competitive survey; this file focuses narrowly on **patch/create reliability** and concrete next steps.
