# Canonical `blueprint_graph_patch` recipe

The engine applies patches **atomically**: if any op fails, **nothing** from that call is kept (`applied_partial` is empty and the transaction is cancelled).

For **`create_node`**, prefer **`semantic_kind`** (`branch`, `execution_sequence`, `call_library_function`, `variable_get`, `variable_set`, `event_override`, `custom_event`, `dynamic_cast`) over raw **`k2_class`** when possible; **`k2_class` wins** if both are set. Use **`validate_only`: true** to dry-run the **full** **`ops[]`** batch and get **`failed_op_index`** (plus optional **`failed_op`**) without mutating the asset — same fields appear on real apply failures.

### Character movement / jump (common triple-jump mistakes)

| Goal | `function_name` (override or call) | `outer_class_path` / `class_path` |
|------|-------------------------------------|-------------------------------------|
| Override **Jump** / **Landed** / **ReceiveJump** on a Character BP | `Jump`, `Landed`, `ReceiveJump` | `/Script/Engine.Character` |
| **`K2Node_CallFunction`** for **`Jump`** | `Jump` | `/Script/Engine.Character` (not `CharacterMovementComponent`) |

Omitting **`outer_class_path`** is OK for those event names when the host can default it. Wrong class (e.g. MovementComponent + `Jump`) is repaired in the resolver when the blueprint parent is Character; still prefer the table above.

## Steps (in order)

1. **Read the graph:** `blueprint_graph_introspect` (preferred for pins + `linked_to`). Never invent `node_guid` values.
2. **Reference existing nodes** in `connect` / `break_link` / `set_pin_default` as **`guid:XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX.PinName`** or **bare `UUID.PinName`** (from introspect). **Never** use story names, display titles, or legacy **`__UAI_G_*`** placeholders.
3. **New nodes:** each `create_node` needs **`patch_id`** + **`k2_class`** or **`semantic_kind`**. Then:
   - **`K2Node_CallFunction`:** `class_path` + `function_name`
   - **`K2Node_VariableGet` / `K2Node_VariableSet`:** `variable_name` (member must exist before use; add with `add_variable` **earlier in the same `ops[]`**)
   - **`K2Node_Event` (override):** `event_override` with `function_name` + `outer_class_path`
   - **Integer compare / math:** use **`K2Node_CallFunction`** with **`class_path`:** `/Script/Engine.KismetMathLibrary` and **`function_name`:** `Less_IntInt`, `Add_IntInt`, etc.
4. **Wire:** `connect` uses **only** `"from":"NodeRef.pin"` and `"to":"NodeRef.pin"`. **`from` must be an OUTPUT pin** and **`to` an INPUT**—the host does **not** auto-reverse. Prefer pin **`name`** from introspect. **`K2Node_VariableSet`:** use **`blueprint_graph_list_pins`** on that node if unsure. **Splicing:** prefer **`splice_on_link`** over guessing coordinates.
5. **`add_variable`:** must include **`type`** (e.g. `"int"`) or resolver aliases — name alone is rejected.
6. **Validate:** `blueprint_compile` and `blueprint_verify_graph` after substantive edits.

## Placement and `auto_layout`

- When **`auto_layout`** is **true** (default), the formatter lays out **only** nodes created in that patch. The plugin may shift clusters and resolve overlaps per **Blueprint Formatting** settings. **`add_variable`** accepts **`name`** or **`variable_name`**. Prefer **`x`/`y`** from **`blueprint_graph_introspect`** near the splice when you need precise placement.
- Set **`auto_layout`: false** to avoid repositioning new nodes.

## Failure payloads (`status: patch_errors` / `patch_validate_errors`)

Expect **`errors[]`**, **`error_codes[]`**, **`failed_op_index`** (0-based into **`ops[]`**), optional **`failed_op`** (or large **`failed_op_json`** truncation), **`applied_partial` always `[]`** on apply failures, and **`suggested_correct_call`** matched to the first error (pin listing, Character event/call example, etc. — not a random default node).

## Batch size and huge payloads

Keep each **inline** `ops[]` modest when streaming JSON is tight.

- **Large op lists:** UTF-8 file under **`Saved/...`** or **`harness_step/...`**, root = JSON array of ops; call **`blueprint_graph_patch`** with **`ops_json_path`** only (mutually exclusive with non-empty inline **`ops[]`**).
