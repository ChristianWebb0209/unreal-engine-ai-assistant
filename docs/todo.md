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

- Having the user-facing buttons to format automatically is not a top priority, since that isn't the main goal of this plugin
- We must focus on the planning/blueprint-formatter-expansions.md, which documents a ton of ways we will expand the agent's abilities to write super well formed and well documented blueprints
- [x] Blueprint formatter expansions: data-wire knots, multi-strand lane layout, `graph_comment` IR + comment-box reflow, and selection formatting (`blueprint_format_selection` + `UnrealAi.BlueprintFormatSelection` console command).
- [ ] Verify Blueprint editor toolbar/menu integration for selection formatting reliability across UE 5.7 minors.
- [x] Vector DB UI: add vector top-graph search + drill-down inspector panel in `SUnrealAiEditorSettingsTab`.
- - A key feature is getting the agent to recognize when functionality can be abstracted into custom events 

## Release Readiness Testing

- [ ] Strict PIE lifecycle: rerun `tests/strict-tests/run-strict-headed.ps1 -Suite strict_catalog_runtime_render_gap_v1` until it passes. Recent history shows `pie_stop` teardown can leave `playing_in_editor` / `play_session_in_progress` true even after the stop call, and the latest attempt was paused after a harness stall (`Harness turns 0/2`) so we can revisit PIE start/stop prompts (making viewport captures a last-resort tool, not the primary way to infer `pie_status`) and harden crash-proofing before the next run.
- [ ] Strict tool contract smoke: rerun `tests/strict-tests/run-strict-headed.ps1 -Suite strict_tool_catalog_coverage_v1` and confirm `strict_assertions_fail_count == 0`.
- [ ] Strict safety/guardrails: rerun `tests/strict-tests/run-strict-headed.ps1 -Suite strict_natural_autonomous_discovery_v1` to ensure “refusal on unsafe or under-specified mutations” still behaves correctly.
- [ ] Qualitative end-to-end regression: run `tests/qualitative-tests/run-qualitative-headed.ps1 -Suite passed-tests.json`
- [ ] Qualitative end-to-end regression: run `tests/qualitative-tests/run-qualitative-headed.ps1 -Suite regression-watchlist.json`
- [ ] Qualitative end-to-end regression: run `tests/qualitative-tests/run-qualitative-headed.ps1 -Suite pre-release-natural-gaps.json`
- [ ] Qualitative continuity/stress: run at least `tests/qualitative-tests/run-qualitative-headed.ps1 -Suite context-continuity-corridor-build.json` and confirm no tool retries/truncations lead to missing `run_finished`.
- [ ] Embedding/retrieval integration (missing coverage): add a targeted strict suite or unit test that exercises `FOpenAiCompatibleEmbeddingProvider` success, HTTP timeout, invalid payload/JSON, and fallback behavior to lexical retrieval.
- [ ] Context assembly around project tree sampling (missing coverage): add a strict suite that forces the “preferred package path / context blurb / sampler refresh” path (via create/rename or preferred-path decisions) and asserts it never invents `/Game/...` paths and never produces orphan `role=tool` rows.
- [ ] Startup ops status + persistence/resume (missing coverage): restart editor and verify startup gating/ops status does not block chat/harness initialization; then run a harness scenario that resumes a plan/DAG and confirm node status persistence matches expectations.
