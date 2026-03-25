# Chat renderer (Unreal AI Editor plugin)

Slate UI that renders **transcript blocks** (user, thinking, assistant, tools, todo plans, notices) from [`FUnrealAiChatTranscript`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiChatTranscript.h), fed by [`FUnrealAiChatRunSink`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/FUnrealAiChatRunSink.h) implementing [`IAgentRunSink`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/IAgentRunSink.h).

## Data flow

1. **User send** — [`SChatComposer`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SChatComposer.cpp) calls `SChatMessageList::AddUserMessage`, then `IUnrealAiAgentHarness::RunTurn` with `FUnrealAiChatRunSink(Transcript)`.
2. **Harness** — streams [`FUnrealAiLlmStreamEvent`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/UnrealAiAgentTypes.h) (`AssistantDelta`, `ThinkingDelta`, tool rounds, finish).
3. **Sink** — updates the transcript; structural changes rebuild the scroll list; assistant/thinking deltas update the active widgets for smooth output.
4. **Stop** — [`SChatComposer`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SChatComposer.cpp) send/stop control (same button: stop icon while a turn is in progress) calls `IUnrealAiAgentHarness::CancelTurn()`; [`OnRunFinished(false, …)`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/FUnrealAiChatRunSink.cpp) ends with a notice row (cancelled vs error).

## Features

| Feature | Implementation |
|--------|------------------|
| **Tool rows** | [`SToolCallCard`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SToolCallCard.cpp) — category-colored bullet before the tool name (tint from [`UnrealAiClassifyToolVisuals`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiToolUi.cpp)), subtle border pulse while `bToolRunning`; args via [`UnrealAiTruncateForUi`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiToolUi.cpp). |
| **Tools per assistant segment** | Each **assistant** block is a segment. [`UnrealAiCollectToolsAfterAssistant`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiChatTranscript.cpp) gathers following `ToolCall` rows in order until the next assistant or user message. [`SAssistantToolsDropdown`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SAssistantToolsDropdown.cpp) shows **Tools (N)** next to the assistant row when N &gt; 0; the menu lists tool names in call order with the same category-colored bullets. |
| **Streaming** | `UUnrealAiEditorSettings::bStreamLlmChat` → [`UnrealAiTurnLlmRequestBuilder`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/UnrealAiTurnLlmRequestBuilder.cpp) sets `FUnrealAiLlmRequest::bStream`. |
| **Typewriter** | [`SAssistantStreamBlock`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SAssistantStreamBlock.cpp) + `bAssistantTypewriter` / `AssistantTypewriterCps` in settings. |
| **Markdown (assistant)** | [`UnrealAiChatMarkdown`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiChatMarkdown.cpp) — leading code-fence wrappers stripped, headings (`#`–`###`), bullets, GitHub-style task lists (`- [ ]` / `- [x]`), inline `**` stripped; styled rows (not raw monospace). |
| **Reasoning subline** | [`SThinkingSubline`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SThinkingSubline.cpp) — one muted italic line **below** the assistant bubble when reasoning exists; animated `.` / `..` / `...` / `..` while waiting for tokens. |
| **Scroll** | Deferred `ScrollToEnd` after content changes ([`SChatMessageList`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SChatMessageList.cpp)). |
| **Harness rounds** | `OnContinuation` does **not** add chat rows (internal multi-round counter only). `reasoning` / `reasoning_content` streams as `ThinkingDelta` → [`SThinkingSubline`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SThinkingSubline.cpp) ([`FOpenAiCompatibleHttpTransport`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Transport/FOpenAiCompatibleHttpTransport.cpp)). |
| **Todo plans** | Harness hook for `agent_emit_todo_plan` → [`STodoPlanPanel`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/STodoPlanPanel.cpp). |
| **Continuation** | `OnTodoPlanEmitted` and other orchestration hooks (see harness). |

## Related

- [`AGENT_HARNESS_HANDOFF.md`](AGENT_HARNESS_HANDOFF.md) — harness iteration entry; console runs and `run.jsonl`.
- [`context-management.md`](context-management.md) — planning artifacts; continuation UX aligns with harness events below.
