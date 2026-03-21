# Context service (Unreal AI Editor plugin)

In-editor **context assembly** for LLM requests: **no vector search** in v1 (see [`agent-and-tool-requirements.md`](../agent-and-tool-requirements.md) §1.3). This is a **logical service** inside the plugin process—not a network backend ([`PRD.md`](../PRD.md) §2.3).

## Role

- Holds **per-chat** curated state: attachments, bounded tool-result memory, optional **editor snapshot** (level selection, Content Browser path/selection, open asset editors).
- **`BuildContextWindow`** returns **`FAgentContextBuildResult`** (`SystemOrDeveloperBlock`, `ContextBlock`, warnings) for the **agent harness** to merge into the **system** message on each LLM request (see [`agent-harness.md`](agent-harness.md)).

### Harness vs context service

| | **Context service** | **Agent harness** |
|--|----------------------|-------------------|
| **Owns** | `context.json`, attachments, tool-result *snippets*, editor snapshot | `conversation.json` (full chat roles for the API), turn loop, tool round-trips |
| **Per turn** | Supplies `SystemOrDeveloperBlock` + `ContextBlock` | Prepends a **system** message built from that output + runs **user/assistant/tool** history |

Do not duplicate: context stays the **editor-specific** layer; the harness owns **orchestration** and **LLM wire format**.

**Planning (v1):** A **`FUnrealAiComplexityAssessor`** feeds optional lines into the built context block; the **canonical `unreal_ai.todo_plan`** (when present) should live in **`context.json`** (e.g. `activeTodoPlan`) so execution sub-turns can use **summary + pointer** prompts without re-sending the full JSON each time — see [`complexity-assessor-todos-and-chat-phases.md`](complexity-assessor-todos-and-chat-phases.md) and [`agent-and-tool-requirements.md`](../agent-and-tool-requirements.md) §1.5.
- Persists to **local disk** so reopening a thread restores context state (not full message history—that remains the chat log).
- **Module shutdown** calls **`FlushAllSessionsToDisk()`** so all in-memory sessions are written before exit.

## On-disk layout

Under `%LOCALAPPDATA%\UnrealAiEditor\` (Windows; see PRD §2.5):

```text
chats/<project_id>/threads/<thread_id>/context.json
```

- **`project_id`**: stable id from current `.uproject` path ([`UnrealAiProjectId`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/UnrealAiProjectId.cpp)).
- **`thread_id`**: GUID string (one per chat tab / composer). **New chat** in the composer generates a new GUID and starts a fresh thread (previous thread is saved first).

## JSON schema (`context.json`)

| Field | Type | Notes |
|-------|------|--------|
| `schemaVersion` | number | Current: **2** (v1 files still load; `activeAssetPath` migrates into `contentBrowserSelectedAssets` when needed) |
| `attachments` | array | `{ type, payload, label }` — `type`: `asset` \| `file` \| `text` \| `bp_node` |
| `toolResults` | array | `{ toolName, truncatedResult, timestamp ISO8601 }` |
| `editorSnapshot` | object? | See below |
| `maxContextChars` | number | Optional per-thread override; `0` = use build options |

### `editorSnapshot` (schema v2)

| Field | Type | Notes |
|-------|------|--------|
| `selectedActorsSummary` | string | Level viewport selection (actor labels) |
| `activeAssetPath` | string | Legacy / first selected asset |
| `contentBrowserPath` | string | Current Content Browser path (`GetCurrentPath(Virtual)`) |
| `contentBrowserSelectedAssets` | string[] | Selected assets in Content Browser (bounded) |
| `openEditorAssets` | string[] | Assets with open editor tabs (`UAssetEditorSubsystem`, bounded) |
| `valid` | bool | Snapshot was populated |

## UI integration ([`SChatComposer.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SChatComposer.cpp))

- **Send**: `LoadOrCreate` → **`@` mention parsing** ([`UnrealAiContextMentionParser`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/UnrealAiContextMentionParser.cpp)) → `RefreshEditorSnapshotFromEngine` → `BuildContextWindow` → prepend to prompt.
- **Attach selection**: adds Content Browser selected assets as attachments (`UnrealAiEditorContextQueries::AddContentBrowserSelectionAsAttachments`).
- **New chat**: `SaveNow` on current thread, new `ThreadId`, `LoadOrCreate` for empty thread.

### `@` mentions

Regex `@([A-Za-z0-9_./]+)`: resolves **full soft object paths** first, then **asset name** search under `/Game` via Asset Registry (`FARFilter` + `GetAssets`).

## Budgets and modes

- **`FAgentContextBuildOptions`**: `MaxContextChars` (default 32k), `EUnrealAiAgentMode` (Ask omits tool results).
- **Truncation**: global character cap on the formatted block (v1: single `TruncateToBudget` on the full block).
- **Caps**: `UnrealAiEditorContextQueries` limits Content Browser selection and open-editor lists (see `MaxContentBrowserSelectedAssets` / `MaxOpenEditorAssets`).

## Code map

| Area | Location |
|------|----------|
| Types / options | [`AgentContextTypes.h`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/AgentContextTypes.h) |
| Editor queries | [`UnrealAiEditorContextQueries.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/UnrealAiEditorContextQueries.cpp) |
| @ parsing | [`UnrealAiContextMentionParser.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/UnrealAiContextMentionParser.cpp) |
| Format + trim | [`AgentContextFormat.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/AgentContextFormat.cpp) |
| JSON | [`AgentContextJson.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/AgentContextJson.cpp) |
| Service | [`FUnrealAiContextService`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/FUnrealAiContextService.cpp) |
| Persistence API | [`IUnrealAiPersistence`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Backend/IUnrealAiPersistence.h) — `SaveThreadContextJson` / `LoadThreadContextJson` |
| Shutdown flush | [`UnrealAiEditorModule.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/UnrealAiEditorModule.cpp) — `FlushAllSessionsToDisk` |

## Future (not v1)

- Embeddings / local vector store ([`agent-and-tool-requirements.md`](../agent-and-tool-requirements.md) §1.3–§1.4).
- LLM-based summarization of tool history when over budget.
- Subscriptions to tab/folder changes (today: **send-time** snapshot only).
