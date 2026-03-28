# Template tokens (harness-injected)

These are **not** sent verbatim to the model. Document them in one place so prompt authors and C++ stay aligned.

| Token | Source | Purpose |
|-------|--------|---------|
| `{{CONTEXT_SERVICE_OUTPUT}}` | `IAgentContextService::BuildContextWindow` | Editor snapshot, attachments summary, tool-result memory, warnings. |
| `{{AGENT_MODE}}` | settings / UI | `ask` \| `agent` \| `plan` — drives tool allowlist and the **`02-operating-modes.md`** section injected at build time. |
| `{{ACTIVE_TODO_SUMMARY}}` | `context.json` → `activeTodoPlan` | Short human summary when a plan exists. |
| `{{PLAN_POINTER}}` | harness | `threadId=…; storage=context.json activeTodoPlan + todoStepsDone` (see `UnrealAiPromptBuilder.cpp`). |
| `{{TOOL_PACK_NAME}}` | harness | Optional label for routed packs (core vs domain). |
| `{{MAX_PLAN_STEPS}}` | harness policy | e.g. `12` — model must not exceed when emitting plans. |
| `{{CONTINUATION_ROUND}}` | harness | e.g. `2 / 5` for UI honesty (optional line in sub-turns). |
