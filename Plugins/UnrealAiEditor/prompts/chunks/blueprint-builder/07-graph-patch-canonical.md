# Canonical `blueprint_graph_patch` recipe

The engine applies patches **atomically**: if any op fails, **nothing** from that call is kept (`applied_partial` is empty and the transaction is cancelled).

For **`create_node`**, prefer **`semantic_kind`** (`branch`, `execution_sequence`, `call_library_function`, `variable_get`, `variable_set`, `event_override`, `custom_event`, `dynamic_cast`) over raw **`k2_class`** when possible; **`k2_class` wins** if both are set. Use **`validate_only`: true** as a **top-level** argument on **`blueprint_graph_patch`** (alongside **`blueprint_path`** / **`ops[]`**) to dry-run the **full** **`ops[]`** batch and get **`failed_op_index`** (plus optional **`failed_op`**) without persisting the asset: the host **spawns** batch-local **`create_node`** instances on the graph, validates **`connect`** pins (and runs **`TryCreateConnection`** when **both** ends are those transient nodes), then **removes** them. **`connect`** involving a **saved** graph node only checks pin names and output→input direction (it does not alter saved wires). Same diagnostic fields appear on real apply failures.

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
2. **Reference existing nodes** in `connect` / `break_link` / `set_pin_default` as **`guid:XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX.PinName`** or **bare `UUID.PinName`** (from introspect). **Never** use story names, display titles, or legacy **`__UAI_G_*`** placeholders. For **`set_pin_default`**, the literal goes in **`value`** (resolver also accepts **`default_value`** as an alias).
3. **New nodes:** each `create_node` needs **`patch_id`** + **`k2_class`** or **`semantic_kind`** (the merged tool catalog lists both on the op so resolver validation accepts `semantic_kind`). Then:
   - **`K2Node_CallFunction`:** `class_path` + `function_name`
   - **`K2Node_VariableGet` / `K2Node_VariableSet`:** `variable_name` (member must exist before use; add with `add_variable` **earlier in the same `ops[]`**)
   - **`K2Node_Event` (override):** `event_override` with **`function_name`**; **`outer_class_path`** optional for common names (e.g. `Landed` / `Jump` default to Character) — set it explicitly when in doubt
   - **Integer compare / math:** use **`K2Node_CallFunction`** with **`class_path`:** `/Script/Engine.KismetMathLibrary` and **`function_name`:** `Less_IntInt`, `Add_IntInt`, etc.
4. **Wire:** `connect` uses **only** `"from":"NodeRef.pin"` and `"to":"NodeRef.pin"`. **`from` must be an OUTPUT pin** and **`to` an INPUT**—the host does **not** auto-reverse. Prefer pin **`name`** from introspect. **`K2Node_VariableSet`:** use **`blueprint_graph_list_pins`** on that node if unsure. **`connect_exec`:** `{ "op":"connect_exec", "from_node":"n1", "to_node":"n2" }` (or **`from`** / **`to`** aliases) wires the **only** visible **exec output** on the source to the **only** visible **exec input** on the target; fails with a clear error if ambiguous—then use explicit **`connect`**. **Editing existing chains:** prefer **`splice_on_link`** (break one link, insert a node, reconnect) over guessing **`connect`** targets on a crowded graph.
5. **`add_variable`:** must include **`type`** (e.g. `"int"`) or resolver aliases — name alone is rejected.
6. **Validate:** `blueprint_compile` and `blueprint_verify_graph` after substantive edits.

## Placement and `auto_layout`

- When **`auto_layout`** is **true** (default), the formatter lays out **only** nodes created in that patch. The plugin may shift clusters and resolve overlaps per **Blueprint Formatting** settings. **`add_variable`** accepts **`name`** or **`variable_name`**. Prefer **`x`/`y`** from **`blueprint_graph_introspect`** near the splice when you need precise placement.
- Set **`auto_layout`: false** to avoid repositioning new nodes.

## Failure payloads (`status: patch_errors` / `patch_validate_errors`)

Expect **`errors[]`**, **`error_codes[]`**, **`failed_op_index`** (0-based into **`ops[]`**), optional **`failed_op`** (or large **`failed_op_json`** truncation), **`applied_partial` always `[]`** on apply failures, and **`suggested_correct_call`** matched to the first error (pin listing, Character event/call example, etc. — not a random default node). On many **`connect`** / **`connect_exec`** pin failures, the payload also includes **`available_pins_from`** and **`available_pins_to`** (each pin: **`name`**, **`direction`**, **`category`**, optional **`default_value`**) so the next patch can copy exact pin names.

## Batch size and huge payloads

Keep each **inline** `ops[]` modest when streaming JSON is tight.

- **Large op lists:** UTF-8 file under **`Saved/...`** or **`harness_step/...`**, root = JSON array of ops; call **`blueprint_graph_patch`** with **`ops_json_path`** only (mutually exclusive with non-empty inline **`ops[]`**).
