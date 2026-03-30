# Todo List

## Subagents and plan mode

- [x] Plan mode parent-thread state writes now use explicit context APIs (`SetActivePlanDagForThread`, `SetPlanNodeStatusForThread`, `ReplaceActivePlanDagWithFreshNodeResetForThread`) so node runs cannot mutate the wrong thread session.
- [x] Editable DAG before execution remains supported (`ApplyDagJsonForBuild` / build panel path).
- [x] Automatic replan on node failure is implemented and bounded (`bPlanAutoReplan`, per-run attempt cap).
- [x] Deterministic fail-mode routing is implemented for node failures (validation/tool-budget failures skip dependents; stream/transport/empty-assistant failures can trigger auto-replan).
- [x] Persistence/resume path now preserves existing node statuses when resuming from DAG and flushes context state at plan finish.
- [x] Guarded wave scheduling prep is in place behind plugin setting `agent.useSubagents` (`FUnrealAiEditorModule::IsSubagentsEnabled`) with deterministic wave selection + diagnostics; execution remains single-child serial until true concurrent dispatch is enabled.
- [ ] Add optional per-node verification hooks (e.g., required tool seen before success).
- [ ] Upgrade guarded wave prep into true concurrent child execution with merge ordering and stricter conflict policy.

## Blueprint Organizer

- We are in the process of unifying the plugins so everything just lives here
- Having the user-facing buttons to format automatically is not a top priority, since that isn't the main goal of this plugin
- We must focus on the planning/blueprint-formatter-expansions.md, which documents a ton of ways we will expand the agent's abilities to write super well formed and well documented blueprints
- [x] Blueprint formatter expansions: data-wire knots, multi-strand lane layout, `graph_comment` IR + comment-box reflow, and selection formatting (`blueprint_format_selection` + `UnrealAi.BlueprintFormatSelection` console command).
- [ ] Verify Blueprint editor toolbar/menu integration for selection formatting reliability across UE 5.7 minors.
- [x] Vector DB UI: add vector top-graph search + drill-down inspector panel in `SUnrealAiEditorSettingsTab`.
- - A key feature is getting the agent to recognize when functionality can be abstracted into custom events 

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