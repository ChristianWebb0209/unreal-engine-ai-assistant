# Todo List

## Core Functionality

- None of the main tools really seem to work too well. 

### Problems List:

- I made a sphere named MySphere and asked agent to focus. It responded by first doing a fuzzy search. Our context window is way too narrow at the start. At least the most likely scene objects, maybe including most recently created, should be included as default context. Success metric for this is that query should know to call just one tool, actor focus in scene, because it knows the scene enough from default.
- Simply saying "make me a cool blueprint" failed completely. My plugin really sucks.

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

- Having the user-facing buttons to format automatically is not a top priority, since that isn't the main goal of this plugin
- We must focus on the planning/blueprint-formatter-expansions.md, which documents a ton of ways we will expand the agent's abilities to write super well formed and well documented blueprints
- [x] Blueprint formatter expansions: data-wire knots, multi-strand lane layout, `graph_comment` IR + comment-box reflow, and selection formatting (`blueprint_format_selection` + `UnrealAi.BlueprintFormatSelection` console command).
- [ ] Verify Blueprint editor toolbar/menu integration for selection formatting reliability across UE 5.7 minors.
- [x] Vector DB UI: add vector top-graph search + drill-down inspector panel in `SUnrealAiEditorSettingsTab`.
- - A key feature is getting the agent to recognize when functionality can be abstracted into custom events 
