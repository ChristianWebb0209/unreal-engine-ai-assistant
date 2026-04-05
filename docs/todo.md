# Todo List:

- I made a sphere named MySphere and asked agent to focus. It responded by first doing a fuzzy search. Our context window is way too narrow at the start. At least the most likely scene objects, maybe including most recently created, should be included as default context. Success metric for this is that query should know to call just one tool, actor focus in scene, because it knows the scene enough from default.
- Text glitch where it bunches up and doesn't animate dropping down correctly
- Editor follow shouldn't be able to be enabled on two running agents at the same time
- Editor follow should be off by default since it causes a lot of lag
- Subagents should have a more rich indication to the user that they are working
- - Maybe something graph based and each concurrent agent is shown side by side next to each other
- We should have the chats just be named by the first user message, and not do anything complicated with custom prompts to decide a name
- - It named my chat after the second message I sent for some reason?
- (big task) Implement everything in docs\planning\agent-editor-responsiveness-streaming.md
- blueprint_graph_patch still fails tremendously for some reason
- All tools should have names that are shown to the user so it doesn't show a tool call like blueprint_graph_patch
- Agent occasionally switches to front view in viewport for no discernable reason
- We should make agent types for each of the blueprint types, then one is just for general blueprint stuff, instead of having one type that does all different types of blueprint
- We need to make a demo for the project preferrably using OpenScreen

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
