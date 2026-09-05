# Scope

Tools: `editor_get_selection`, `actor_get_material_slots`, `material_instance_set_parameter`, `material_instance_get_scalar_parameter`, `material_instance_get_vector_parameter`, `material_get_usage_summary`, `asset_index_fuzzy_search`.

## Selected actor → material color (happy path)

When the user refers to **selection** (“the floor I selected”, “make this blue”):

1. **`editor_get_selection`** (`{}`) — ground on outliner selection; do not start with `scene_fuzzy_search`.
2. **`actor_get_material_slots`** — pass **`actor_path`** from selection; read **`material_path`** and **`vector_parameter_names`**.
3. **`material_instance_set_parameter`** — **`value_kind: vector`**, **`linear_color: [r,g,b,a]`** (0–1). Example blue: `[0,0,1,1]`. Pick **`parameter_name`** from step 2 (e.g. `Color`).

Do **not** pass RGB as `value:{r,g,b,a}` or as `value_kind: scalar`. Base Materials require duplicating to a Material Instance first.

Close with `<unreal_ai_specialist_result>...</unreal_ai_specialist_result>`.
