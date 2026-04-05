# Canonical `blueprint_graph_patch` recipe

The engine applies patches **atomically**: if any op fails, **nothing** from that call is kept (`applied_partial` is empty and the transaction is cancelled).

### Catalog scope (no T3D / Tier-1 IR)

**T3D import/export** and **Tier-1 IR apply** are **not** in the agent tool roster—dispatch returns **`not_implemented`** for those tool ids. All Kismet edits use **`blueprint_graph_introspect`** (read) and **`blueprint_graph_patch`** (write), then **`blueprint_compile`** / **`blueprint_verify_graph`**. Follow **`01-deterministic-loop.md`**. **`__UAI_G_*`** placeholders are **invalid** in patch node refs; use real GUIDs or in-batch **`patch_id`** (see **`06-cross-tool-identity.md`** for the ref grammar and examples).

For **`create_node`**, prefer **`semantic_kind`** (`branch`, `execution_sequence`, `call_library_function`, `variable_get`, `variable_set`, `event_override`, `custom_event`, `dynamic_cast`) over raw **`k2_class`** when possible; **`k2_class` wins** if both are set. Use **`validate_only`: true** as a **top-level** argument on **`blueprint_graph_patch`** (alongside **`blueprint_path`** / **`ops[]`**) to dry-run the **full** **`ops[]`** batch and get **`failed_op_index`** (plus optional **`failed_op`**) without persisting the asset: the host **spawns** batch-local **`create_node`** instances on the graph, validates **`connect`** pins (and runs **`TryCreateConnection`** when **both** ends are those transient nodes), then **removes** them. **`connect`** involving a **saved** graph node only checks pin names and output→input direction (it does not alter saved wires). Same diagnostic fields appear on real apply failures.

### `semantic_kind` is a **closed set**

**Do not invent** values such as **`literal_int`**, **`literal_float`**, **`int_literal`**, or **`call_function`** (that name is not a `semantic_kind`; use **`call_library_function`**). If the model emits `literal_*`, the resolver may rewrite it to **`call_library_function`** + **`/Script/Engine.KismetSystemLibrary`** + **`MakeLiteralInt`** / **`MakeLiteralFloat`** / **`MakeLiteralBool`** and insert **`set_pin_default`** on **`patch_id.Value`**—still prefer the canonical shape below.

### Integer / float / bool constants (literals)

Use **`create_node`** with **`semantic_kind`** **`call_library_function`**, **`class_path`** **`/Script/Engine.KismetSystemLibrary`**, **`function_name`** **`MakeLiteralInt`** (or **`MakeLiteralFloat`**, **`MakeLiteralBool`**), then **`set_pin_default`** on **`yourPatchId.Value`** (or wire the **ReturnValue** output to another node’s data pin with **`connect`**). **Do not** use **`KismetMathLibrary`** for **`MakeLiteral*`**—those nodes live on **`KismetSystemLibrary`**. Integer **math/compare** (`Add_IntInt`, `Less_IntInt`, …) stays on **`KismetMathLibrary`**. All of these are **data-only** (no **`Execute`** pin) unless a node is a special latent case.

### Execution vs data wiring

- **`connect_exec`** (or **`connect`** between **exec** pins only): use **only** between nodes that expose a **white** execution **output** and **input** (e.g. **Branch**, **Sequence**, **VariableSet**, **`Character.Jump`** **CallFunction**).  
- **`KismetMathLibrary`** and **`KismetSystemLibrary`** **pure** nodes (**`Add_IntInt`**, **`Less_IntInt`**, **`MakeLiteralInt`**, …) have **no** **`Execute`** pin—**never** aim **`connect_exec`** at them. Run the **exec chain** through branch/sequence/custom flow; use **`connect`** for **A**, **B**, **ReturnValue**, etc.  
- Ground every pin name in **`blueprint_graph_introspect`** or **`blueprint_graph_list_pins`**.

### `splice_on_link` ordering

Each **`splice_on_link`** needs **`from`**, **`to`**, and **`insert_patch_id`**. The **`create_node`** that defines **`insert_patch_id`** must appear **earlier** in the same **`ops[]`** array than the **`splice_on_link`** that references it.

### `K2Node_Event` overrides (Jump, Landed, Tick, …)

These nodes are **entry points**: they have an outgoing execution pin (typically **`then`**), **not** an incoming **`execute`** pin. You **cannot** chain **`SomeNode.then → JumpEvent.execute`** — that pin does not exist. Put initialization on **`ReceiveBeginPlay`** (or similar) as a **separate** execution chain. To perform the default jump from custom logic, use **`K2Node_CallFunction`** with **`class_path`** **`/Script/Engine.Character`** and **`function_name`** **`Jump`**.

### Character movement / jump (common triple-jump mistakes)

| Goal | `function_name` (override or call) | `outer_class_path` / `class_path` |
|------|-------------------------------------|-------------------------------------|
| Override **Jump** / **Landed** / **ReceiveJump** on a Character BP | `Jump`, `Landed`, `ReceiveJump` | `/Script/Engine.Character` |
| **`K2Node_CallFunction`** for **`Jump`** | `Jump` | `/Script/Engine.Character` (not `CharacterMovementComponent`) |

Omitting **`outer_class_path`** is OK for those event names when the host can default it. Wrong class (e.g. MovementComponent + `Jump`) is repaired in the resolver when the blueprint parent is Character; still prefer the table above.

## Steps (in order)

1. **Read the graph:** `blueprint_graph_introspect` (preferred for pins + `linked_to`). Never invent `node_guid` values.
2. **Reference existing nodes** in `connect` / `break_link` / `set_pin_default` as **`guid:…`** or **bare `UUID.PinName`** (from introspect). **Never** use story names or display titles. For **`set_pin_default`**, the literal goes in **`value`** (resolver also accepts **`default_value`** as an alias). Ref grammar and examples: **`06-cross-tool-identity.md`**.
3. **New nodes:** each `create_node` needs **`patch_id`** + **`k2_class`** or **`semantic_kind`** (the merged tool catalog lists both on the op so resolver validation accepts `semantic_kind`). Then:
   - **`K2Node_CallFunction`:** `class_path` + `function_name`
   - **`K2Node_VariableGet` / `K2Node_VariableSet`:** `variable_name` (member must exist before use; add with `add_variable` **earlier in the same `ops[]`**)
   - **`K2Node_Event` (override):** `event_override` with **`function_name`**; **`outer_class_path`** optional for common names (e.g. `Landed` / `Jump` default to Character) — set it explicitly when in doubt
   - **Integer compare / math:** use **`K2Node_CallFunction`** with **`class_path`:** `/Script/Engine.KismetMathLibrary` and **`function_name`:** `Less_IntInt`, `Add_IntInt`, etc.
4. **Wire:** `connect` uses **only** `"from":"NodeRef.pin"` and `"to":"NodeRef.pin"`. **`from` must be an OUTPUT pin** and **`to` an INPUT**—the host does **not** auto-reverse. Prefer pin **`name`** from introspect. **`K2Node_VariableSet`:** use **`blueprint_graph_list_pins`** on that node if unsure. **`connect_exec`:** `{ "op":"connect_exec", "from_node":"n1", "to_node":"n2" }` (or **`from`** / **`to`** aliases) wires the **only** visible **exec output** on the source to the **only** visible **exec input** on the target—**both nodes must be exec-capable** (not pure **`KismetMathLibrary`** data nodes). If ambiguous, use explicit **`connect`** for named exec pins. **Editing existing chains:** prefer **`splice_on_link`** (break one link, insert a node, reconnect) over guessing **`connect`** targets on a crowded graph.
5. **`add_variable`:** must include **`type`** (e.g. `"int"`) or resolver aliases — name alone is rejected.
6. **Validate:** `blueprint_compile` and `blueprint_verify_graph` after substantive edits.

## Placement and `auto_layout`

- When **`auto_layout`** is **true** (default), the plugin runs **one** post-batch layout pass (layered DAG / Sugiyama-style crossing reduction—not per-op nudging). **`layout_scope`**:
  - **`patched_nodes`** (default): lay out only nodes created in that patch (**`create_node`** / **`create_comment`**), then translate the cluster toward **exec-linked neighbors** and push overlapping non-patch script nodes **right** (AABB + gap).
  - **`patched_nodes_and_downstream_exec`**: union those seeds with nodes reachable by following **exec outputs** on the same graph (hard cap ~384 nodes; **`layout_downstream_truncated`** / **`layout_warnings`** if capped). Use after **`splice_on_link`**, **`break_link`**, or interjecting into an exec chain so **downstream** tails reflow. For this scope, per-node **`x`/`y`** hints from **`create_node`** are **not** applied to the layout pass (**`layout_hints_ignored`: true**).
  - **`full_graph`**: reformat the entire script graph—only when an explicit whole-graph tidy is intended (can disturb manual layout unless **Blueprint Formatting → preserve existing** is on).
- **`layout_anchor`** (with local scopes only; ignored for **`full_graph`**): **`neighbor`** (default) keeps neighbor translation after layout; **`below_existing`** first shifts the laid-out set **down** so it clears the bottom of existing script nodes (Autonomix-style “inject below the slab”).
- Local layout is **skipped** when the scoped set has **at most one** script node (**`layout_local_skipped`: true**) to avoid pointless moves (Autonomix also skips tiny injected sets). When skipped, **`layout_applied`** is **false** even if **`auto_layout`** was **true**.
- On **success**, the tool JSON echoes **`layout_scope`** and **`layout_anchor`** and may include **`layout_hints_ignored`**, **`layout_downstream_extra_nodes`**, **`layout_downstream_truncated`**, **`layout_local_skipped`**, and optional **`layout_warnings[]`** (e.g. downstream cap)—use these to see whether tails were reflowed or hints were dropped.
- Unknown **`layout_scope`** / **`layout_anchor`** values fail **before** any op runs (resolver **`validation_failed`** or dispatch **`error`**), not as **`status: patch_errors`**.
- Set **`auto_layout`: false** to preserve positions entirely for that patch (same spirit as Autonomix **`auto_layout: false`**).
- Resolver aliases: **`layout_scope`** `downstream` / `patched_plus_downstream` / `patch_and_tail` → **`patched_nodes_and_downstream_exec`**; **`layout_anchor`** `below` → **`below_existing`**.
- **`add_variable`** accepts **`name`** or **`variable_name`**. Prefer **`x`/`y`** from **`blueprint_graph_introspect`** near the splice when you need precise placement (honored only when hints are not ignored).

## Failure payloads (`status: patch_errors` / `patch_validate_errors`)

Expect **`errors[]`**, **`error_codes[]`**, **`failed_op_index`** (0-based into **`ops[]`**), optional **`failed_op`** (or large **`failed_op_json`** truncation), **`applied_partial` always `[]`** on apply failures, and **`suggested_correct_call`** matched to the first error (pin listing, Character event/call example, etc. — not a random default node). On many **`connect`** / **`connect_exec`** pin failures, the payload also includes **`available_pins_from`** and **`available_pins_to`** (each pin: **`name`**, **`direction`**, **`category`**, optional **`default_value`**) so the next patch can copy exact pin names.

## Batch size and huge payloads

Keep each **inline** `ops[]` modest when streaming JSON is tight.

- **Large op lists:** UTF-8 file under **`Saved/...`** or **`harness_step/...`**, root = JSON array of ops; call **`blueprint_graph_patch`** with **`ops_json_path`** only (mutually exclusive with non-empty inline **`ops[]`**).
