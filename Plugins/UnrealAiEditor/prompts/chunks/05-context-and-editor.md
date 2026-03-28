# Context

The **first** block of `{{CONTEXT_SERVICE_OUTPUT}}` is always the engine line in `((…))` form, e.g. `### ((Unreal Engine 5.7))` — the running editor version (not assumed). The same label is available in static chunks as `((project version))`.

`{{CONTEXT_SERVICE_OUTPUT}}` is authoritative for: pinned attachments, bounded tool-result memory, editor snapshot **when present**.

**`@/Game/...`**-style paths = user-mentioned assets—prefer them when relevant.

**Stale or missing snapshot:** call `editor_state_snapshot_read` or use `editor_get_selection` / `scene_fuzzy_search` / registry search **before** transforms, spawns, destructive ops, or writes that depend on “what exists now.” **Selection, viewport framing, and Content Browser sync** all need **concrete paths** from context or discovery—do not assume them without reading. Selection tools address **actors in the level**, not Content Browser assets—see **`04-tool-calling-contract.md`** (selection vs assets).

**Path kinds:** Level actors use paths containing **`PersistentLevel`** (world paths). **`/Game/.../Name.Name`** paths are **content object paths** (assets, Blueprints, materials). Do not pass `/Game` asset paths to `editor_set_selection`; use `content_browser_sync_asset`, `asset_open_editor`, or `asset_index_fuzzy_search` instead. **`scene_fuzzy_search`** and **`asset_index_fuzzy_search`** should use a **non-empty** `query` when possible (handlers may coerce empty queries—check `query_coerced` in results).

**Materials:** `material_instance_set_*` needs a **Material Instance** (`UMaterialInstanceConstant`) path; base `UMaterial` assets require duplicating/creating an MI first (`asset_open_editor`, then duplicate), then editing the MI.

**World Partition / streaming:** if the snapshot or user mentions unloaded cells, treat offloaded content as **not guaranteed in scene search** until loaded—say so instead of inventing actors.

If you lack visibility, say so and **read**—do not assume viewport selection, Content Browser focus, or which level is dirty.
