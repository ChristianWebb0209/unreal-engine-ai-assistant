# Chat renderer (Unreal AI Editor plugin)

Slate UI that renders **transcript blocks** (user, thinking, assistant, tools, todo plans, notices) from [`FUnrealAiChatTranscript`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiChatTranscript.h), fed by [`FUnrealAiChatRunSink`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/FUnrealAiChatRunSink.h) implementing [`IAgentRunSink`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/IAgentRunSink.h).

## Data flow

1. **User send** — [`SChatComposer`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SChatComposer.cpp) calls `SChatMessageList::AddUserMessage`, then `IUnrealAiAgentHarness::RunTurn` with `FUnrealAiChatRunSink(Transcript)`.
2. **Harness** — streams [`FUnrealAiLlmStreamEvent`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/UnrealAiAgentTypes.h) (`AssistantDelta`, `ThinkingDelta`, tool rounds, finish).
3. **Sink** — updates the transcript; structural changes rebuild the scroll list; assistant/thinking deltas update the active widgets for smooth output.
4. **Stop** — [`SChatHeader`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SChatHeader.cpp) **Stop** calls `IUnrealAiAgentHarness::CancelTurn()`; [`OnRunFinished(false, …)`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/FUnrealAiChatRunSink.cpp) ends with a notice row (cancelled vs error).

## Features

| Feature | Implementation |
|--------|------------------|
| **Tool rows** | [`SToolCallCard`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SToolCallCard.cpp) — category-colored bullet before the tool name (tint from [`UnrealAiClassifyToolVisuals`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiToolUi.cpp)), subtle border pulse while `bToolRunning`; args via [`UnrealAiTruncateForUi`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiToolUi.cpp). |
| **Tools per assistant segment** | Each **assistant** block is a segment. [`UnrealAiCollectToolsAfterAssistant`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiChatTranscript.cpp) gathers following `ToolCall` rows in order until the next assistant or user message. [`SAssistantToolsDropdown`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SAssistantToolsDropdown.cpp) shows **Tools (N)** next to the assistant row when N &gt; 0; the menu lists tool names in call order with the same category-colored bullets. |
| **Streaming** | `UUnrealAiEditorSettings::bStreamLlmChat` → [`UnrealAiTurnLlmRequestBuilder`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/UnrealAiTurnLlmRequestBuilder.cpp) sets `FUnrealAiLlmRequest::bStream`. |
| **Typewriter** | [`SAssistantStreamBlock`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SAssistantStreamBlock.cpp) + `bAssistantTypewriter` / `AssistantTypewriterCps` in settings. |
| **Scroll** | Deferred `ScrollToEnd` after content changes ([`SChatMessageList::RequestScrollToEnd`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SChatMessageList.cpp)). |
| **Thinking** | `reasoning_content` / `reasoning` in OpenAI-compatible JSON ([`FOpenAiCompatibleHttpTransport`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Transport/FOpenAiCompatibleHttpTransport.cpp)) → `ThinkingDelta` → muted [`SThinkingBlock`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SThinkingBlock.cpp). |
| **Todo plans** | Harness hook for `agent_emit_todo_plan` → [`STodoPlanPanel`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/STodoPlanPanel.cpp). |
| **Continuation** | `IAgentRunSink::OnRunContinuation` / `OnTodoPlanEmitted` (orchestration can call later). |

## Related

- [`agent-harness.md`](agent-harness.md) — harness and `conversation.json`.
- [`complexity-assessor-todos-and-chat-phases.md`](complexity-assessor-todos-and-chat-phases.md) — planning loop UX.
