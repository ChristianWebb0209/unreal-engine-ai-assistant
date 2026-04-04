# Canonical `blueprint_graph_patch` recipe (primary path)

Use this sequence for **small, surgical** EventGraph edits. The engine applies patches **atomically**: if any op fails, **nothing** from that call is kept (`applied_partial` is empty and the transaction is cancelled).

## Steps (in order)

1. **Read the graph:** `blueprint_graph_introspect` (preferred for pins + `linked_to`) and/or `blueprint_export_ir`. Never invent `node_guid` values.
2. **Reference existing nodes** in `connect` / `break_link` / `set_pin_default` as **`guid:XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX.PinName`** or **bare `UUID.PinName`** (from export). **Never** use story names, display titles, or `__UAI_G_*` placeholders (those are T3D-only).
3. **New nodes:** each `create_node` needs **`patch_id`** + **`k2_class`** (full `/Script/...` path or bare `K2Node_*`, repaired by the host). Then:
   - **`K2Node_CallFunction`:** `class_path` + `function_name`
   - **`K2Node_VariableGet` / `K2Node_VariableSet`:** `variable_name` (member must exist before use; add with `add_variable` **earlier in the same `ops[]`**)
   - **`K2Node_Event` (override):** `event_override` with `function_name` + `outer_class_path`
   - **Integer compare / math:** there is no `K2Node_IntLess` / `K2Node_IntAdd`. Use **`K2Node_CallFunction`** with **`class_path`:** `/Script/Engine.KismetMathLibrary` and **`function_name`:** `Less_IntInt`, `Add_IntInt`, `Greater_IntInt`, `EqualEqual_IntInt`, etc. (not `CoreUObject` or a bogus `K2Node_CallFunction` class_path).
4. **Wire:** `connect` uses **only** `"from":"NodeRef.pin"` and `"to":"NodeRef.pin"`. **`from` must be an OUTPUT pin** on its node and **`to` must be an INPUT**—the host does **not** auto-reverse; swap the strings if you get a direction error. The resolver may fold **`link_from` / `link_to`** or **`from_node`+`from_pin`** into those fields before validation—still prefer canonical `from`/`to`. Prefer pin **`name`** from introspect (exact `PinName`); duplicate display titles can be rejected as ambiguous. Exec pins accept **`then`**, **`else`**, **`execute`** (normalized); **do not** wire a branch’s **false** output back into its own **then** (invalid). **Enhanced Input** nodes use real pin names from introspect (e.g. **`Started`**, **`Triggered`**) with the **`guid:…`** or **`node_guid`** prefix.
   - **`K2Node_VariableSet`:** the value pin is usually named like the variable (**`JumpCount`**, not **`Set`**). Use **`blueprint_graph_list_pins`** on that node if unsure. Aliases **`Set` / `Value` / `Input`** are repaired when possible.
   - **`K2Node_Event` (e.g. `OnLanded`):** `event_override` must include **`function_name`** (e.g. **`Landed`**) and **`outer_class_path`** (e.g. **`/Script/Engine.Character`** for a Character BP). Wrong outer → node creation fails.
   - **Splicing into an existing exec wire:** prefer **`splice_on_link`** (breaks one link and inserts a node) over guessing coordinates. The insert node is positioned at the midpoint between upstream/downstream node bounds before layout; **`auto_layout`** then runs the usual patch layout, including **AABB overlap push** so non-patch nodes that geometrically overlap the new cluster shift **right**.
5. **`add_variable`:** must include **`type`** (e.g. `"int"`) or **`variable_type`**—name alone is rejected.
6. **Validate:** `blueprint_compile` and `blueprint_verify_graph` (e.g. `links`, `orphan_pins`) after substantive edits.

## Placement, `auto_layout`, and inserting into an existing chain

- When **`auto_layout`** is **true** (default), the formatter lays out **only** nodes created in that patch. If those nodes have **no** event entry inside the patch (typical when you splice after **Enhanced Input** or another existing node), the internal layout used to pack that subset at **X≈0**, far from the rest of the graph. The plugin then **shifts that cluster** using **node bounding boxes** next to **wired exec predecessors** (or centroid / guarded successor placement — never a huge negative slide). It then **pushes every non-patch script node whose box overlaps the patch cluster** (plus separation gap) **right**, iterating until clear—not limited to a narrow vertical band. If you set **non-zero `x`/`y`** on **`create_node`**, those coordinates are **kept** (no extra translation); overlap-push still runs on **other** nodes. **`add_variable`** accepts **`name`** or **`variable_name`** (resolver copies **`variable_name` → `name`** when needed). Prefer **`x`/`y`** from **`blueprint_graph_introspect`** near the splice when you need precise placement.
- To **avoid** any automatic repositioning of new nodes, set **`auto_layout`: false** (you then own all coordinates).
- For **`blueprint_apply_ir`**, **`variables[].variable_type`** must match the tool schema (usually a **string** like **`"int"`** or a compact pin-type object per catalog)—do not invent unsupported shapes or validation will fail before dispatch.

## Failure payloads (`status: patch_errors`)

Expect **`errors[]`** (strings), parallel **`error_codes[]`**, **`applied_partial` always `[]`**, and **`suggested_correct_call`** (often `blueprint_graph_patch` with a minimal `ops[]` example, or `blueprint_graph_list_pins` when pins are wrong). Resolver failures still use **`validation_errors`** / **`suggested_correct_call`** as today.

## Batch size and huge payloads

Keep each **inline** `ops[]` modest (tens of ops) when the client streams tool JSON in small chunks—very long single calls can hit provider output limits.

- **Hundreds of ops or multi‑MB JSON:** write the op list to a **UTF-8** project file whose **root value is a JSON array** of the same op objects as `ops[]`, under **`Saved/...`** or **`harness_step/...`**, then call `blueprint_graph_patch` with **`ops_json_path`** set to that **project-relative** path (e.g. `Saved/UnrealAi/my_patch_ops.json`). **Do not** set a non-empty inline `ops[]` in the same call—**mutually exclusive**.
- **Bulk layout / whole-graph authoring:** prefer **T3D** export → edit → preflight → import (`04-t3d-placeholders-and-import.md`) when that workflow fits; use **`blueprint_graph_patch`** (inline or **`ops_json_path`**) for **surgical** edits and wiring.
