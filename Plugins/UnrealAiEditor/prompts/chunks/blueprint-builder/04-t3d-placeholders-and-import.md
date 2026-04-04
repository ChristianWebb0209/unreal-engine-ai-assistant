# T3D / IR authoring (deprecated for agent roster)

**T3D export/import and Tier-1 IR apply are not in the agent tool catalog.** Unknown or removed tool ids are answered with `not_implemented` from dispatch.

Use **`blueprint_graph_introspect`** for node GUIDs and wiring, **`blueprint_graph_patch`** for all Kismet mutations (including **`add_variable`** and **`connect`** in the same batch), then **`blueprint_compile`** / **`blueprint_verify_graph`**. See **`07-graph-patch-canonical.md`** and **`01-deterministic-loop.md`**.

Legacy `__UAI_G_*` tokens are **not** valid in `blueprint_graph_patch` node refs — use `guid:…` from introspect or `patch_id` from the current ops batch.
