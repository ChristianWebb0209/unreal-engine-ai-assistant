# Todo List

## Context Manager

- Audit context manager and create testing harness to assert what gets added and removed from context at various steps.
- **Harness (initial):** [docs/CONTEXT_HARNESS.md](docs/CONTEXT_HARNESS.md), `tests/context_workflows/`, `scripts/run-headed-context-workflows.ps1`, `tests/bundle_context_workflow_review.py`, console `UnrealAi.DumpContextWindow`.
- - Use our harness for testing tooling as a model, and create scripts to let AIs run headed editor and view context at any point.
- - Get agent to build a set of prompts, then run a full run through and build a QUALITIATIVE analysis of how our context manager is working.

## Blueprint Organizer (In separate plugin)

- We need to ensure that there are two buttons added at the top of the blueprint editor event graph: organize all, and (only if user has selected something), organize selected. Both buttons need to have icons and tooltip hints.

## Tooling

- Ensure tools are working for PERFORMANT and FAST: multi-select, select by class, fuzzy search for selection (within scene or within file tree).
- - Ensure fast workflow for batch operations like replace material on every wall in scene, etc. We should make a testing situation for this.
- Ensure tools catalog has high coverage of editor functionality:
- - Data assets, animation basics, building components within components (building an actor then attatching a collision mesh, etc.)

## Plan / Orchestrate Mode

- Probably rename orchestrate mode to plan mode
- - We need to brainstorm, because it seems like maybe agent mode will be too weak, and the only way to do complex tasks will be with plan mode. Should we make agent mode able to make plans behind the scenes if it deems complexity high enough?
- Maybe we should remove our existing complexity analysis, or at least do some tests to see if it is helping anything or just confusing the AI
- DAG validation: does our DAG orchestration work with not allowing certain tasks to start until previous ones have been completed? Let's make some test cases for this.
- Worker orchestration
- - Ensure we establish reasonable budgets. Maybe this should be configurable by model as well?
- - Conflict detection between subagents is OUT OF SCOPE
- - - We will handle this in a way that doesn't guarantee safety by simply having the initial prompt decide how it will spawn subagents, and telling it to keep in mind how editing the same assets will lead to conflicts. It should create a DAG where each subagent doesn't conflict, but this could be a source of errors in the future.

## General Agent Tasks

- Work more on and verify stall behavior. Right now stalling is a major issue, and it seems like there is something wrong with the model service layer itself.

# Future Features

- "Asset Fetcher" setting. When toggled, it will enable a tool (which corresponds with a service) that will interface with FAB or some other asset library to allow LLM to get free fairuse assets, import them into project, and wire them up.
- - This will be super hard because FAB doesn't expose a lot of useful APIs for us to use. We may have to do a hybrid approach, where our assistant will open a FAB instance and listen for what the user selects, then try to make use of whatever the user imports.
- Stage changes before making them. Add checkpoints and reverting capabilities.
- Unreal docs RAG
- Unreal forums RAG
- Support for other versions, ideally all 5.x verisions. Currently this only works for 5.7.