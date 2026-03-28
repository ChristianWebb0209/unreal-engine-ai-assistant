# Planning in Unreal AI Editor

This document complements [`context-management.md`](context-management.md) by describing **two** supported planning mechanisms and how they differ.

## A. Plan mode (chat / harness): `unreal_ai.plan_dag`

- **Trigger:** User selects **Plan** in the chat composer, or headless `UnrealAi.RunAgentTurn` / `UnrealAiHarnessScenarioRunner::RunAgentTurnSync` with mode `plan`.
- **Planner turn:** One LLM completion in **Plan** mode with **no tools**; the model outputs JSON for the DAG (`nodes`, dependencies, optional `schema: unreal_ai.plan_dag`).
- **Execution:** `FUnrealAiPlanExecutor` parses and validates the DAG, persists it via `IAgentContextService::SetActivePlanDag`, then runs **one Agent-mode harness turn per ready node** (serial scheduling in v1). Child runs use derived thread ids `<parent>_plan_<nodeId>`.
- **Not used:** `agent_emit_todo_plan` is **not** required for this high-level DAG; the plan is assistant JSON, not a tool call.

## B. Agent mode: `agent_emit_todo_plan` → `unreal_ai.todo_plan`

- **Trigger:** Model invokes the **`agent_emit_todo_plan`** tool during normal **Agent** turns (complexity / replan loops, `todo_plan_only` nudges, etc.).
- **Persistence:** `SetActiveTodoPlan` stores the todo plan; step checkboxes and summaries are separate from the Plan-mode DAG.
- **Execution:** Standard agent loop with harness nudges—there is **no** separate `FUnrealAiPlanExecutor` path for this artifact.

## Headless parity

`RunAgentTurnSync` with `Mode == Plan` uses the same **`FUnrealAiPlanExecutor`** entry point as the editor chat so automation exercises planner + node execution, not a single planner-only turn.
