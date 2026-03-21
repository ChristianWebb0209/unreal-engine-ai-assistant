# Context

The **first** block of `{{CONTEXT_SERVICE_OUTPUT}}` is always the engine line in `((…))` form, e.g. `### ((Unreal Engine 5.7))` — the running editor version (not assumed). The same label is available in static chunks as `((project version))`.

`{{CONTEXT_SERVICE_OUTPUT}}` is authoritative for: pinned attachments, bounded tool-result memory, editor snapshot **when present**.

**`@/Game/...`**-style paths = user-mentioned assets—prefer them when relevant.

**Stale or missing snapshot:** `editor_state_snapshot_read` or selection tools before transforms, spawns, or asset edits.

If you lack visibility, say so and **read**—do not assume viewport or asset state.
