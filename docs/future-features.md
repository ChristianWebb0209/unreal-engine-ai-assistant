# Future Features

- "Asset Fetcher" setting. When toggled, it will enable a tool (which corresponds with a service) that will interface with FAB or some other asset library to allow LLM to get free fairuse assets, import them into project, and wire them up.
- - This will be super hard because FAB doesn't expose a lot of useful APIs for us to use. We may have to do a hybrid approach, where our assistant will open a FAB instance and listen for what the user selects, then try to make use of whatever the user imports.
- Stage changes before making them. Add checkpoints and reverting capabilities.
- Unreal docs RAG
- Unreal forums RAG
- Support for other versions, ideally all 5.x verisions. Currently this only works for 5.7.

## Context

- We need to think about the prompt chunks more and find some more aggressive ways to prune them without losing accuracy. Our current agents take like a whole minute for a simple task, and they use so many tokens.

## Tooling

- Ensure tools are working for PERFORMANT and FAST: multi-select, select by class, fuzzy search for selection (within scene or within file tree).
- - Ensure fast workflow for batch operations like replace material on every wall in scene, etc. We should make a testing situation for this.
- Ensure tools catalog has high coverage of editor functionality:
- - Data assets, animation basics, building components within components (building an actor then attatching a collision mesh, etc.)
- (Consider) We should redo the entire tools catalog. I think we can implement some basic semantic patterns like get_ tools where the model knows that each get_ tool has a corresponding set_ tool, we could also make a unified set_setting tool that has a specific schema to indicate which type of setting it is (based on UEs literal schemea), like viewport (change from lit to unlit), project settings, editor settings. 
- - We should analyze how unreal exposes functions to set and get all different types of settings to build this tool, and try to find similar patterns to reduce the complexity of other bunches of tools.

## PCG / World Building

This is an optional expansion (not core MVP) that enables the agent to build and iteratively modify open-world-ish environments using Unreal’s Procedural Content Generation (PCG) framework.

The goals are:
- Translate a natural-language “world brief” into a layered distribution plan (biomes, layers, densities, constraints).
- Generate the PCG setup (PCG assets/graphs/components + supporting spline blueprints/aux assets).
- Allow delta-style updates (update only requested layers/constraints without regenerating everything from scratch).
- Provide enough editor-side introspection for the agent to select meshes and parameterize PCG graph nodes safely.

### Optional toggle / gating (keep it off by default)

We should add a dedicated enable switch in our existing plugin settings so PCG/world-building tools and PCG indexing are only active when the user opts in.

Suggested UI/behavior:
- `Project Settings -> Plugins -> Unreal AI Editor` adds `Enable PCG / World Building Expansion` (default `false`).
- When off:
  - The PCG tool pack is removed from the model tool eligibility allow-list (or the pack is never added to the prompt tool roster).
  - PCG-specific indexing/caching is skipped (no background scans of large asset sets).
- When on:
  - The PCG tool pack becomes available to Agent/Plan modes.
  - The context assembly includes a PCG-aware “asset catalog” candidate set (described below).

Implementation notes (how it plugs into the existing architecture):
- The tool roster is already mode-gated via the catalog + eligibility pipeline; the PCG pack should be another tiered pack that can be swapped in/out.
- The context service should be extended to optionally produce PCG candidates (but without making baseline chat/tools slower when the feature is off).

### What we need to index differently (world-building context)

Core issue: our default context assembly is great for “what’s currently in editor + what tool results already exist”, but PCG world-building needs a larger, structured view of “what assets exist that can populate world layers, and what constraints/parameters they support”.

We should build a lightweight, cached “PCG Asset Catalog” index that is separate from the general retrieval/vector index.

Index inputs (derived from Asset Registry + cheap asset metadata reads):
- Candidate mesh assets:
  - Static meshes used for environment props (trees, rocks, ground cover, grass, foliage meshes).
  - Record bounding box size, typical scale (if available/cheap), whether collision data exists, and a “category guess” derived from naming conventions and/or asset tags.
- Candidate material assets:
  - Optional: material instances used to bias selection for biome variants (e.g., snow vs summer).
  - Optional: parameter hints to help density/LOD selection (only if cheap and stable).
- Candidate PCG-related assets:
  - PCG graph templates we generate/store for common patterns (trees+ground cover layering, spline-rail vegetation, rock scatter with exclusion zones).
  - A registry of supported graph parameter schemas (seed, density, spacing/scale ranges, filters).
- Candidate “layer semantics”:
  - A mapping from prompt-layer names (canopy/understory/ground/rocks) to asset categories and PCG graph node groups.
  - This can start as heuristic rules and be refined over time.

Index outputs (what context assembly should use):
- A deterministic “asset category table” the agent can refer to when proposing a world layout.
- A “layer-to-asset category” suggestion list (top-K per layer) so the agent can quickly choose assets that exist.
- Optional constraints summaries:
  - Which assets are too large/small for certain layers.
  - Which assets are likely to be expensive (heuristics: triangle count, LOD presence) if we can read them cheaply.

Where the index is stored:
- Cache the computed catalogs on disk under the plugin data root (to avoid re-scanning the project every run).
- Add “index versioning” so we can safely rebuild when schemas/tools change.

How this interacts with our existing context/retrieval design:
- Treat PCG Asset Catalog as a PCG-specific candidate source that feeds into the same context ranking/packing flow.
- Keep it off by default; only build/use it when the PCG expansion toggle is enabled.
- Optional future: if we later add semantic/embedding-based asset suggestions, it should be additive on top of the deterministic catalog (not the only source of truth).

### Agent workflows enabled by this expansion

World brief -> distribution plan:
- Agent parses the user request into:
  - biome/layer set
  - desired densities per layer
  - constraints (around objects, avoid areas, follow splines, elevation bands)
  - variation strategy (seed strategy and asset variety)

Plan -> PCG graph instantiation:
- Agent selects (from the PCG Asset Catalog):
  - which meshes populate each layer category
  - which PCG template graph matches the requested constraint style
  - parameter values (seed, scale range, density)
- Agent then generates/updates:
  - PCG graph asset(s)
  - PCG component(s) placed into the level (or attached to a spline blueprint)
  - any supporting spline blueprint wrapper if the world brief includes spline-driven placement

Delta update:
- Agent produces an update plan that modifies only the requested layers/constraints.
- The tools should support “parameter edits” and “node-level updates” rather than delete/rebuild always.

Verification loop:
- Agent reads back a compact graph summary and execution result status (warnings/errors).
- On failures, agent applies repair steps (e.g., re-run with safer density bounds, swap incompatible asset categories).

### Tooling we would need (PCG/world-building tool pack)

Below are proposed tool categories (tool ids can be refined later). The intent is to give the agent enough structured levers to build and modify PCG graphs safely, while keeping the rest of the system simple.

1. PCG asset catalog & selection helpers
- `pcg_get_asset_catalog` (read):
  - returns cached categories, candidates per category, and cheap constraints metadata.
- `pcg_guess_asset_category` (read):
  - optional helper that returns category guesses + confidence for a mesh path.

2. PCG graph template management
- `pcg_list_templates` (read):
  - returns known PCG graph templates and their supported parameter schemas.
- `pcg_build_graph_ir` (write):
  - produces an IR object for a graph instance based on the distribution plan (layer set + constraints).
- `pcg_apply_graph_ir` (write):
  - instantiates/updates the PCG graph asset and returns anchors/IDs of created/modified nodes.

3. Level integration: placement wrappers
- `pcg_create_spline_blueprint_wrapper` (write):
  - creates/updates a spline-based wrapper blueprint (used when placement is along splines).
- `pcg_attach_pcg_component` (write):
  - attaches/updates a `UPCGComponent`-like object and binds it to the correct graph + parameters.

4. Parameter updates (delta-friendly)
- `pcg_set_layer_parameters` (write):
  - updates density/spacing/scale ranges per layer.
- `pcg_set_constraint_parameters` (write):
  - updates exclusion zones, “around X” filters, elevation bands, and spline-based influence settings.
- `pcg_execute_graph` (exec or write depending on how we model it):
  - triggers PCG execution in editor and returns a structured status + any warnings.

5. Readbacks & diagnostics
- `pcg_get_graph_summary` (read):
  - returns a bounded summary of layers, selected asset categories, key parameter nodes, and node counts.
- `pcg_get_execution_warnings` (read):
  - returns warnings produced by PCG execution (missing assets, invalid bounds, invalid parameter ranges).

6. (Optional, staged) Open-world / scaling primitives
- `world_partition_setup_for_pcg` (exec/write, staged):
  - prepares world partition cells/streaming layout to support PCG regionization.
- `pcg_place_world_region` (write, staged):
  - creates PCG setup per region/cell with seed offsets to avoid visible repetition.

Safety and budget rules for this expansion:
- All mutating PCG tools should be gated behind Agent/Plan modes, and should require user confirm for high-impact operations (deleting/replacing large graph structures).
- Density/scale edits should be range-clamped using the asset catalog constraints metadata to prevent runaway performance regressions.

### MVP vs staged scope

Suggested staged delivery:
- Stage 1 (minimal): generate PCG graphs from a prompt into a single level area using a small set of templates + delta parameter edits.
- Stage 2: spline-driven placement and multi-layer iteration (canopy/understory/ground/rocks) with asset catalog selection.
- Stage 3: region/cell scaling for more open-world layouts (World Partition integration and seed offset strategy).