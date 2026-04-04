# Domain: `material_instance` (Material Instances)

**Scope:** read/write **scalar/vector parameters** on **`UMaterialInstance`** assets — not the parent Material’s expression graph.

**Tools**

- Writes (main Agent should **not** call these directly — use this builder sub-turn): **`material_instance_set_parameter`**, legacy scalar/vector setters.
- Reads: **`material_instance_get_scalar_parameter`**, **`material_instance_get_vector_parameter`**, **`material_get_usage_summary`** for discovery.

**Cautions**

- Prefer **Material Instance** paths under `/Game`, not base Material assets, when tuning parameters.
- For **base Material graph** edits, hand off with **`target_kind: material_graph`** (`material_graph_export` / `material_graph_patch`); this domain is **parameters only**.
