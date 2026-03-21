# Frontend (client-side) implementation guide — Unreal AI Editor plugin

This document **inventories** all user-facing surfaces in the plugin today, **maps** them to product docs ([`PRD.md`](../PRD.md), [`agent-and-tool-requirements.md`](../agent-and-tool-requirements.md), [`context-service.md`](context-service.md), [`chat-renderer.md`](chat-renderer.md), [`complexity-assessor-todos-and-chat-phases.md`](complexity-assessor-todos-and-chat-phases.md), [`agent-harness.md`](agent-harness.md)), and gives a **concrete backlog** to finish client-side work.

**Scope:** Slate UI in `Plugins/UnrealAiEditor/` — not backend/tool handlers unless the UI must call them.

---

## 1. Current user-facing inventory

### 1.1 Window menu (`Window → Unreal AI → …`)

Registered in [`UnrealAiEditorModule.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/UnrealAiEditorModule.cpp) (`RegisterMenus`):

| Menu item | Tab ID | Spawn widget |
|-----------|--------|----------------|
| Agent Chat | `UnrealAiEditorTabIds::ChatTab` | [`SUnrealAiEditorChatTab`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorChatTab.cpp) |
| AI Settings | `SettingsTab` | [`SUnrealAiEditorSettingsTab`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorSettingsTab.cpp) |
| API Keys & Models | `ApiModelsTab` | [`SUnrealAiEditorApiModelsTab`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorApiModelsTab.cpp) |
| Quick Start | `QuickStartTab` | [`SUnrealAiEditorQuickStartTab`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorQuickStartTab.cpp) |
| Help | `HelpTab` | [`SUnrealAiEditorHelpTab`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorHelpTab.cpp) |

**Gap vs PRD §5.2:** PRD also mentions **Tools → Agent Chat**; today only **Window → Unreal AI** is wired (unless commands bind elsewhere — see §1.3).

### 1.2 Editor commands

[`UnrealAiEditorCommands`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/App/UnrealAiEditorCommands.cpp) defines `OpenChatTab`, `OpenSettingsTab`, etc., and **`Register()`** is called from the module — but there is **no** `FUICommandList::MapAction` in the plugin yet (window menu uses `TryInvokeTab` directly). **Shortcut keys and a command palette (PRD §5.1 Ctrl+K)** are still missing.

**Action:** Add a command list (e.g. in module startup or a `FUnrealAiEditorModule` helper) mapping each command to `FGlobalTabmanager::Get()->TryInvokeTab(...)`, and bind default chords if desired.

### 1.3 Project Settings

**Edit → Project Settings → Plugins → Unreal AI Editor** registers [`UUnrealAiEditorSettings`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Public/UnrealAiEditorSettings.h) via `RegisterSettings` in the module.

**Current fields:** agent defaults, OpenRouter-ish fields, **chat** toggles (`bStreamLlmChat`, `bAssistantTypewriter`, `AssistantTypewriterCps`).

**Gap vs PRD §5.3 / §8.3:** Full matrix (paths to external CLIs, Node path, profiles table, MCP port, env vars) is **not** all represented as first-class UPROPERTYs — much of that is expected to live in **`plugin_settings.json`** edited in the **AI Settings** tab.

### 1.4 Agent Chat tab (primary surface)

**Structure:** [`SUnrealAiEditorChatTab`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorChatTab.cpp) → vertical stack:

1. [`SChatHeader`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SChatHeader.cpp) — profile/agent labels (mostly static text), **Settings**, **New Chat**, **Connect**, **Stop**.
2. [`SChatMessageList`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SChatMessageList.cpp) + [`FUnrealAiChatTranscript`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiChatTranscript.h) — transcript blocks (user, thinking, assistant, tools, todo, notices). See [`chat-renderer.md`](chat-renderer.md).
3. [`SChatComposer`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/SChatComposer.cpp) — mode cycle (Ask/Fast/Agent), Attach, New chat, Send.

**Backend wiring:** Send → [`IUnrealAiAgentHarness::RunTurn`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/IUnrealAiAgentHarness.h) + [`FUnrealAiChatRunSink`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/FUnrealAiChatRunSink.h).

### 1.5 API Keys & Models tab

[`SUnrealAiEditorApiModelsTab`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorApiModelsTab.cpp) — grid for provider, base URL, API key, mask toggle, model id, **Save** / **Test connection** (stub paths). Must stay aligned with [`FUnrealAiModelProfileRegistry`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Harness/) and `plugin_settings.json` schema.

### 1.6 AI Settings tab

[`SUnrealAiEditorSettingsTab`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorSettingsTab.cpp) — large **JSON** editor for full `plugin_settings.json` + Save. This is the **power user** surface; PRD also wants a **form** Settings panel from the **gear** (§8.2).

### 1.7 Quick Start & Help

- **Quick Start** — minimal steps + button to open chat; copy should be updated as features land (still says “stub” in places).
- **Help** — placeholder text pointing at repo docs; PRD §8.5 wants **embedded markdown** or external link.

### 1.8 Styling

[`FUnrealAiEditorStyle`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Style/UnrealAiEditorStyle.cpp) — panel brushes; chat uses mixed **inline colors** and **FAppStyle** icons for tools. PRD §9 asks for **tokenized** theme — partial.

---

## 2. What docs require on the client (checklist)

| Doc / section | Client-side requirement | Status (high level) |
|---------------|-------------------------|---------------------|
| **PRD §8.1** Chat header | Profile dropdown (lock while run), Agent dropdown, Connect state, gear → settings, New chat, Help | **Partial** — static labels; Connect toggles stub chat service; **no** profile lock; **no** real agent list |
| **PRD §8.1** Conversation | User/assistant/system; streaming; tool blocks collapsible; thinking collapsible; **permission prompts** | **Partial** — transcript + tools + thinking + Stop; **no** collapsible sections; **no** permission UI |
| **PRD §8.1** Composer | Multiline; Send; **Enter / Shift+Enter**; **@ dropdown**; **attachment chips** | **Partial** — multiline + Send; **@** parsed in text only (no dropdown); attachments **not** shown as chips in composer |
| **PRD §8.1** Footer | Version, optional latency/model | **Missing** |
| **PRD §8.2** Gear settings | Profiles + tools matrix + MCP + env | **Missing** as dedicated panel — JSON tab only |
| **PRD §2.4** Instant durability | Material changes persisted immediately | **Partial** — context flush on shutdown; chat **conversation** persistence via harness; verify **write-ahead** for every message |
| **PRD §5.7** Tool audit | Local log file + UI path to open | **Missing** frontend (may be file-only later) |
| **context-service.md** | @ mentions, attach, snapshot on send | **Implemented** core paths; **UI** could show snapshot summary |
| **chat-renderer.md** | Transcript, streaming, typewriter, scroll, Stop | **Implemented** baseline; optional **stick-to-bottom**, **markdown** |
| **complexity-assessor…** | Complexity block in context; **todo plan** tool; **continuation** non-terminal UX | **Backend/design** partly done; **FUnrealAiComplexityAssessor** **not** implemented; harness **continuation loop** **not** fully implemented; UI **`OnRunContinuation`** stub |
| **agent-harness.md** | Reload LLM on settings save | **Implemented** via registry |

---

## 3. Prioritized backlog — client-only (recommended order)

### Tier A — Ship-quality chat shell

1. **Composer keyboard** — `Enter` send, `Shift+Enter` newline (`SMultiLineEditableTextBox` custom behavior or `OnKeyDownHandler`).
2. **@ mention UX** — popup [`SMenu`](https://dev.epicgames.com/documentation/en-us/unreal-engine/slate-widgets) or list view: query Asset Registry as user types after `@` (debounced). Align with [`UnrealAiContextMentionParser`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Context/UnrealAiContextMentionParser.cpp) resolution rules.
3. **Attachment chips** — horizontal list of `FContextAttachment` / labels above input; remove button; reflect [`context.json`](../docs/context-service.md) attachments.
4. **Tool cards** — collapsible body for args + result (expand/collapse); optional **copy** button; monospace for JSON per PRD §9.3.
5. **Thinking block** — collapsible when non-empty; **don’t** show placeholder “…” when empty (hide row).
6. **Connect / session state** — visual **connected** vs **stub/offline** vs **error**; disable Send when not configured (optional).
7. **Header** — replace static “Profile / Agent” text with real **`SComboBox`** bound to profiles/models from `plugin_settings.json` / registry; implement **profile lock during active harness run** (disable dropdown while `ActiveRunner` or expose `IsTurnInProgress()` on harness).

### Tier B — Planning loop UI (ties to architecture docs)

8. **`FUnrealAiComplexityAssessor`** — not UI, but **surface** the score: optional small badge in header or footer (“Complexity: high”) when context includes it — purely read-only from next `BuildContextWindow` extension.
9. **`agent_emit_todo_plan`** — ensure tool exists in [`UnrealAiToolCatalog.json`](../Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json) + handler; [`STodoPlanPanel`](Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/STodoPlanPanel.cpp) already renders — add **step checkboxes** bound to persisted `activeTodoPlan` in context ([`context-service.md`](context-service.md) schema bump).
10. **Continuation affordance** — when harness runs **sub-turns**, call `IAgentRunSink::OnRunContinuation` and show **“Round 2/5”** strip ([`FUnrealAiChatTranscript::SetRunProgress`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/UnrealAiChatTranscript.cpp) already exists). Wire harness orchestration first, then polish UI.
11. **Auto-continue toggle** — settings checkbox (PRD): “Run plan steps automatically” vs “pause after plan.”

### Tier C — Settings & onboarding parity

12. **Gear → structured Settings** — either embed [`SUnrealAiEditorSettingsTab`](Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorSettingsTab.cpp) in a drawer or split **form fields** (provider, key, model) from raw JSON (advanced).
13. **API Models tab** — live **Test connection** using real HTTP ping (partially stub); **ReloadLlmConfiguration** after save (already in registry paths — verify UI calls it).
14. **Quick Start** — update copy to match real flows (HTTP vs stub, Stop, tools).
15. **Help tab** — embed `docs/` as `FSlateTextBlock` from packaged content, or open external browser to GitHub/docs URL.

### Tier D — Visual design (PRD §9)

16. **Theme tokens** — centralize colors/fonts in [`FUnrealAiEditorStyle`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Style/UnrealAiEditorStyle.h): user vs assistant backgrounds, tool/thinking/error tokens.
17. **Max readable width** — constrain message column (`SBox` max width ~720px centered in scroll).
18. **Markdown / rich text** — optional second phase: `SRichTextBlock` + small markdown subset for assistant messages.

### Tier E — Safety & ops (UI)

19. **Permission prompts** — modal or inline bar: Allow once / Always / Deny — needs **policy state** in context + harness gate before destructive tools.
20. **Tool audit log** — **Open log** button in Help or Settings; path from PRD §5.7 (`Saved/Logs/...` or plugin data dir).

### Tier F — Polish / reference parity

21. **Tools menu** duplicate entry (PRD §5.2) — mirror **Window** entries under **Tools → Unreal AI**.
22. **Global Ctrl+K** — command palette opening chat or doc search (large effort; track separately).
23. **Blueprint graph “Attach to Agent Prompt”** — editor extension, not chat tab only (separate task).
24. **Virtualized message list** — `SListView` for very long threads ([`chat-renderer.md`](chat-renderer.md) risk note).

---

## 4. File map (frontend touchpoints)

| Concern | Primary files |
|---------|----------------|
| Tabs / spawn | [`UnrealAiEditorModule.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/UnrealAiEditorModule.cpp), [`UnrealAiEditorTabIds.h`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Public/UnrealAiEditorTabIds.h) |
| Chat layout | [`SUnrealAiEditorChatTab.cpp`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Tabs/SUnrealAiEditorChatTab.cpp), [`SChatHeader.*`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/), [`SChatComposer.*`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/), [`SChatMessageList.*`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/) |
| Transcript model | [`UnrealAiChatTranscript.*`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/), [`FUnrealAiChatRunSink.*`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/) |
| Widget blocks | [`SToolCallCard.*`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/), [`SThinkingBlock.*`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/), [`SAssistantStreamBlock.*`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/), [`STodoPlanPanel.*`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/Widgets/) |
| Project settings | [`UnrealAiEditorSettings.h`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Public/UnrealAiEditorSettings.h) |
| Menus | [`RegisterMenus`](../Plugins/UnrealAiEditor/Source/UnrealAiEditor/Private/UnrealAiEditorModule.cpp) |

---

## 5. Testing the client (manual)

1. **Window → Unreal AI → Agent Chat** — send message; verify user bubble, assistant streaming/typewriter, tool rows after model returns tools, **Stop** cancels.
2. **API Keys & Models** — save JSON/settings; restart optional; verify harness uses HTTP when key present (see plugin README).
3. **Project Settings → Unreal AI Editor** — toggle stream/typewriter; relaunch chat behavior.
4. **New chat** — clears transcript; thread id rotates; context save on old thread (see [`context-service.md`](context-service.md)).

---

## 6. Related docs

- [`chat-renderer.md`](chat-renderer.md) — implemented renderer behavior.
- [`agent-harness.md`](agent-harness.md) — when UI must trigger reload / cancel.
- [`complexity-assessor-todos-and-chat-phases.md`](complexity-assessor-todos-and-chat-phases.md) — planning UX and persistence.
- [`../PRD.md`](../PRD.md) §8–§9 — full UI inventory and visual spec.

---

## 7. Implementation status (2026-03-20)

The following backlog items from §3 have been **implemented** in `Plugins/UnrealAiEditor/`:

| Tier | Item | Notes |
|------|------|--------|
| A | Composer keyboard (Enter / Shift+Enter) | `SChatComposer` `OnKeyDownHandler` |
| A | @ mention UX | Asset Registry query + inline suggestion list under the input |
| A | Attachment chips | Row above composer; remove (×); syncs with `context.json` attachments |
| A | Tool cards (collapse, copy, monospace JSON) | `SToolCallCard` expandable area + Copy |
| A | Thinking collapsible; no empty placeholder | `SThinkingBlock` `SExpandableArea`; transcript skips empty thinking |
| A | Connect / LLM mode indicator | Header shows HTTP vs stub; chat Connect unchanged |
| A | Header model combo + complexity badge | `SComboBox` for `models{}` keys; heuristic complexity from `BuildContextWindow`; combo disabled while `IsTurnInProgress()` |
| B | Complexity surface | `FAgentContextBuildResult::ComplexityLabel` (low/medium/high heuristic) |
| B | `agent_emit_todo_plan` + catalog + dispatch | `UnrealAiToolCatalog.json`, `UnrealAiDispatch_AgentEmitTodoPlan`, context `SetActiveTodoPlan` |
| B | Todo step checkboxes | `STodoPlanPanel` when plan JSON matches persisted `activeTodoPlan` |
| B | Continuation strip | `OnRunContinuation` emitted at start of LLM round 2+ (`DispatchLlm`) |
| B | Auto-continue toggle | `UUnrealAiEditorSettings::bAutoContinuePlanSteps` (Project Settings; harness pause wiring may follow) |
| C | Gear → structured path | AI Settings tab button opens API Keys & Models tab + hint text |
| C | API test + reload | `FUnrealAiModelConnectorHttp` GET `/models`; Save still calls `ReloadLlmConfiguration` |
| C | Quick Start / Help | Updated copy; Help opens `docs/` and `Saved/Logs` in Explorer |
| D | Theme tokens | `UnrealAiEditor.UserBubble`, `UnrealAiEditor.AssistantLane` + existing brushes |
| D | Max readable width | Message column `MaxDesiredWidth(720)` centered |
| E | Permission prompts | Inline bar with disabled Allow/Deny (policy hooks TBD) |
| E | Tool audit / logs | Help tab **Open Saved/Logs** |
| F | Tools → Unreal AI menu | Mirrors Window submenu |
| F | Global Ctrl+K → chat | `OpenChatTab` chord + `RegisterUnrealAiEditorKeyBindings` via `LevelEditor` command list |

**Tool catalog reload:** `ReloadLlmConfiguration` reloads both `FUnrealAiToolCatalog` (harness) and `FUnrealAiToolExecutionHost::ReloadCatalog()`.

**Still deferred / partial (by design or follow-up):**

- **Markdown / SRichText** for assistant messages — not implemented (plain `STextBlock` / stream block).
- **Virtualized message list** — long threads still use `SVerticalBox` (see `chat-renderer.md` risk note).
- **Blueprint “Attach to Agent Prompt”** — editor extension, out of chat tab scope.
- **Full destructive-tool permission gate** — needs harness policy + context; UI bar is a placeholder.
- **PRD “command palette” (Ctrl+K doc search)** — only **Ctrl+K opens Agent Chat**; full palette is separate work.

---

## Document history

| Date | Author | Note |
|------|--------|------|
| 2026-03-21 | Engineering | Initial inventory + backlog from repo state and PRD. |
| 2026-03-20 | Engineering | §7 implementation status after client backlog pass. |
