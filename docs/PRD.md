# Product Requirements Document (PRD)

**Project:** Built-in AI editor for Unreal Engine (similar capability target: [Agent Integration Kit / NeoStack AI](https://aik.betide.studio/) reference)

**Document version:** 1.3  
**Last updated:** 2026-03-21  
**Sources:** Scraped `docs/aik-editor/**/page.md`, `vids-to-scrape.txt` (NeoStack / AIK-related videos), and Unreal Engine 5 editor extension patterns.

---

## 1. Executive summary

Build an Unreal Engine **editor plugin** that embeds a **multi-agent AI assistant** directly in the editor: native Slate UI, streaming chat, tool execution across Blueprints/materials/behaviors/etc., project context, and strong safety/auditability. The product is **local-first**: **no cloud user accounts** for core functionality; **chats and settings persist only on the user’s machine** (see §2.4–§2.5). Phase 1 targets **feature parity** with the reference product’s documented surface; Phase 2 adds **differentiators** (listed in §12).

**MVP deployment (non-negotiable):** There is **no server and no product backend** of any kind. **All functionality ships inside the Unreal plugin** (editor module + in-editor UI + local persistence). The only network calls are **user-initiated connections to third-party LLM providers** (e.g. OpenRouter, Anthropic, OpenAI) or **optional local tooling** (e.g. MCP clients connecting to **localhost** served by the editor—see §2.3). We do **not** operate a hosted API, sync service, or telemetry backend for MVP unless explicitly added later as an opt-in, separately documented product.

---

## 2. Goals & non-goals

### 2.1 Goals

- **In-editor chat** with **streaming** responses and **tool calls** visible to the user before destructive actions.
- **Bring-your-own-model** via **third-party** APIs (e.g. OpenRouter-style) and/or external agent CLIs **on the user’s machine**; no requirement to use our infrastructure.
- **Deep Unreal integration**: assets, graphs, levels, VFX, animation, input, PCG, audio, etc. (breadth comparable to reference “comparison” claims).
- **Project awareness**: deterministic context (Asset Registry, tool reads, `@` mentions)—**no vector / semantic index in MVP** (see `agent-and-tool-requirements.md` §1.3).
- **Operational quality**: logs, crash diagnostics, updates, stable sessions.

### 2.2 Non-goals (initial)

- **Any product-hosted backend or server** for MVP: no **our** REST API, job queue, account service, sync service, or RAG corpus host. **Everything runs in the editor plugin** except calls to **user-chosen** LLM endpoints.
- Replacing Unreal’s build system or packaging pipeline entirely inside the plugin.
- Running a full cloud IDE; the product remains **local editor + optional HTTPS to third-party model providers** (user-supplied endpoints only—**we do not require signing in to our service**).

### 2.3 MVP architecture (plugin-only, no product backend)

| Layer | MVP rule |
|-------|----------|
| **Product code** | **Unreal editor plugin only** (C++/Slate modules). No separate Node/Python **service** required for core chat/tools. |
| **Persistence** | **On-disk under user profile** (§2.5), written by **in-editor** subsystems—**no** standalone database server process. |
| **LLM traffic** | **HTTPS from editor** (or subprocess you spawn) to **third-party** APIs the user configures. |
| **MCP / local bridge** | If a **localhost** HTTP server is exposed for MCP or external agents, it runs **inside the editor process** (or a child process **bundled with the plugin**), **not** a remote Betide-style cloud. |
| **Future** | A hosted backend (sync, team features, cloud RAG) is **explicitly out of scope for MVP** and would be a separate product decision. |

### 2.4 Local-first mandate (no hosted “product user”)

| Principle | Requirement |
|-------------|-------------|
| **No product sign-in** | No account, email, or “user id” **required** to use chats, settings, or profiles. Third-party LLM/API keys may still be configured by the user (BYOK). |
| **Data residency** | **All** chat history, tool audit metadata, profile definitions, UI layout, and plugin settings are stored **locally** unless the user explicitly opts into a future feature that says otherwise. |
| **Instant durability** | Every material change (new message, edit setting, rename chat, toggle) is **persisted immediately** (no “Save” button for core state). Use **write-ahead** or equivalent so a crash mid-session does not lose the last message. |
| **Secrets** | API keys and tokens are stored **encrypted at rest** on disk (OS keychain/credential manager where possible + encrypted blob fallback). Never log raw secrets. |

### 2.5 Local persistence layout & service (in-editor only for MVP)

**Storage root (per OS)**

| OS | Default root |
|----|----------------|
| **Windows** | `%LOCALAPPDATA%\<ProductName>\` (e.g. `C:\Users\<you>\AppData\Local\<ProductName>\`) |
| **macOS** | `~/Library/Application Support/<ProductName>/` |
| **Linux** (if supported) | `$XDG_CONFIG_HOME/<ProductName>/` or `~/.local/share/<ProductName>/` |

**Recommended subfolders**

| Path | Contents |
|------|----------|
| `settings/` | Global plugin settings, window layout, theme, default agent, feature flags. |
| `chats/` | Per-project or per-workspace conversation stores (see below). |
| `profiles/` | Agent profiles (tool allow-lists, instructions, overrides). |
| `secrets/` | Encrypted key material (or delegate to OS store + pointer files only). |
| `cache/` | **MVP:** general plugin cache (e.g. UI, thumbnails). **Not** used for embeddings in MVP—reserved for future optional local index only. |
| `logs/` | Local diagnostic logs (optional rotation). |

**Scoping chats to Unreal projects**

- Prefer **one conversation store per `.uproject` identity** (hash of project path + engine version optional) so opening different games does not mix threads.
- Store path: `chats/<project_id>/threads/*.jsonl` or SQLite `chats/<project_id>/store.sqlite`.

**Persistence implementation (MVP: inside the plugin only)**

**MVP does not ship** a separate OS-level persistence daemon (no Windows Service, no macOS Launch Agent, no standalone “backend” process for storage). The **same contract** is implemented **inside the editor**:

- A subsystem such as **`FPersistenceSubsystem`** (or equivalent) with **async disk IO off the game thread** and **single-writer** discipline to avoid corrupt concurrent writes from Slate + tools.
- **Append-only or WAL-backed** chat log; **fsync** (or batch + final fsync) for streaming.
- **Transactional** settings updates (atomic file replace).
- **Crash recovery** on startup (WAL replay / checksums / “restore last autosave” if needed).

**Optional later (non-MVP):** a **separate local helper process** for persistence or MCP—still **not** a product cloud server; only if engineering needs justify it.

**Sync semantics**

- **Local-only**: no background upload of chats/settings.
- **User-initiated export/import** (zip of `chats/` + `settings/`) may be added later for backup—still not a cloud account.

---

## 3. Personas

- **Gameplay programmer** — Blueprints, BT/ST, Enhanced Input, replication.
- **Technical artist** — materials, Niagara, sequencer, cinematics.
- **Indie / solo dev** — wants fast iteration, minimal setup.
- **Studio integrator** — needs audit logs, permissions, profile-based tool policies.

---

## 4. Engine & distribution

| Requirement | Detail |
|-------------|--------|
| **Engine** | UE 5.5+ (match reference: 5.5 / 5.6 / 5.7 stated on marketing site). |
| **Distribution** | Fab / manual zip to `Plugins/<PluginName>/` (reference documents both). |
| **Platforms** | Editor Win64 primary; macOS editor secondary if resources allow. |

---

## 5. Information architecture (reference site → our plugin)

The reference documentation is organized as below. Our plugin should expose **equivalent surfaces** (not necessarily identical copy).

### 5.1 Global shell (docs + product)

- **Brand / product name** + **Documentation** entry.
- **Search** with keyboard shortcut (**⌘K** on Mac; **Ctrl+K** on Windows in our implementation).
- **Primary nav** (from scraped pages): **Getting Started** (Installation, Configuration, Quick Start), **Agents** (sub-hub + per-agent pages), **Chat Interface**, **Profiles**, **Resources**, **Troubleshooting**, **Comparison**, **Changelog**.
- **External links**: Discord, GitHub (placeholders or real as product matures).

### 5.2 Editor menus (reference)

- **Tools → Agent Chat** (home page quick example).
- **Window → Agent Chat** (chat-interface page).

Our plugin must register:

- `Window` menu: **Agent Chat** (dockable tab).
- `Tools` menu: shortcut to same tab / command.

### 5.3 Project Settings

Path pattern (reference): **Edit → Project Settings → Plugins → &lt;OurPlugin&gt;**.

**Persistence:** values edited here are **mirrored to local storage** (§2.5) **immediately** on change, not only in `DefaultEngine.ini`—the plugin should treat **AppData (or equivalent) as source of truth** for AI UI settings, while still optionally syncing readable defaults into project config where useful for team sharing (explicit **Export team defaults** action).

Settings to support (from reference configuration doc):

| Area | Settings |
|------|----------|
| **Agent** | Default agent; **Auto Connect**; **Verbose logging** (ACP/protocol style logging). |
| **OpenRouter (or generic API)** | API key; default model string. |
| **Paths** | Paths to external agent binaries; **Node.js path** (Windows note in reference). |
| **Profiles** | Save/load named profiles (agent type, keys, model, env vars). |
| **Environment variables** | Key/value list passed to spawned agent processes. |
| **MCP** (if we ship MCP) | **Server port** (reference default 9315) on **localhost** inside the **editor/plugin**; **Auto start**; protocol notes (Streamable HTTP vs HTTP+SSE). **Not** a remote product backend. |

### 5.4 Agents section (pages to mirror)

Reference sub-pages under **Agents**:

- **Choosing an Agent** (decision matrix / recommendations).
- **Claude Code**, **OpenRouter**, **Codex CLI**, **Gemini CLI**, **Cursor**, **Copilot CLI** (each: prerequisites, install steps, auth, adapter notes).

Our PRD requires **each** to have: onboarding wizard or checklist, “Test connection”, and link to troubleshooting.

### 5.5 Profiles

Reference behavior we must match:

- **Built-in profiles** (example set: Full Toolkit, Animation, Blueprint & Gameplay, Cinematics, VFX & Materials) with **non-deletable** built-ins but editable contents.
- **Custom profiles**: **+ New** from **Settings** (gear in chat).
- **Tool whitelist** model: empty list = all tools; non-empty = strict allow-list.
- **Global disable** in Project Settings overrides profile.
- **Custom instructions** per profile (append to system prompt).
- **Tool description overrides** per profile (e.g. tailor `edit_blueprint` description for animation).
- **Profile dropdown** in chat header; **cannot switch during active session** (reference rule).

### 5.6 Chat Interface

Reference features to implement:

- Dockable **Agent Chat** window.
- **Agent dropdown** + **Connect**.
- **Streaming** responses.
- **Tool call review** (expand panels).
- **@ mentions** for assets with type prefixes (`@BP_`, `@M_`, `@DT_`, etc.) and search dropdown.
- **Right-click in Blueprint graph → Attach to Agent Prompt** (context for next message).
- **Copilot CLI** (if we support) documented similarly.

### 5.7 Troubleshooting

Reference: collect **Tool Audit Log** + engine log paths; instructions to send to Discord/support.

We will implement:

- `Saved/Logs/<Product>_ToolAudit.log` (or similar) structured lines: timestamp, OK/FAIL, tool name, asset, summary.

### 5.8 Comparison & Changelog

- Internal **comparison matrix** page (competitive positioning).
- **Changelog** with versioned sections (“What’s Better”, etc.).

---

## 6. Editor automation tools (AI-callable)

These are **first-class tools** exposed to the model (with permissions), in addition to asset/graph editors.

### 6.1 Screenshot tool

**Purpose:** Capture the current editor viewport (or a named viewport) for vision models, bug reports, before/after reviews.

**Requirements:**

- Capture **active editor viewport**; optional **named viewport** / **PIE** capture flag.
- Options: resolution scale, **include UI** vs **game only**, **HDR** vs **LDR**, **delay N frames** (for GPU catch-up).
- Output: save to project `Saved/Screenshots/AI/<timestamp>_<slug>.png` + return **absolute path** + **resolution** to the model.
- Errors: no active viewport, capture failed, disk full.

**UE5 implementation notes (engineering):**

- `FScreenshotRequest::RequestScreenshot` / viewport client screenshot hooks; high-res via `UThumbnailRenderer`-style patterns or `FViewport::ReadPixels`; for **editor** use `FEditorViewportClient` associated with Level Editor viewport.
- Consider **unreal automation** consistency: use same folder conventions as High Resolution Screenshot when user expects it.

### 6.2 Move / orbit / pan editor camera tool

**Purpose:** Let the AI adjust what the user sees without manual flight—useful for reviews, cinematics checks, level art passes.

**Modes (tool parameters):**

- **Orbit** around world location or selected actor (radius, pitch/yaw delta).
- **Pan** (screen-space or world-space delta).
- **Dolly** / **zoom** (FOV or spring arm analog in editor camera).
- **Set transform** explicitly (location + rotation, optional **ease** time).

**Safety:**

- Clamp deltas; optional **user approval** for large moves.
- Respect **Pilot** vs **unpilot** (if piloting a CameraActor).

**UE5 implementation notes:**

- Operate on **active** `FEditorViewportClient` / `GEditor->GetActiveViewport()` patterns.
- Many teams implement via **Editor Scripting** utilities or direct `FViewportCameraTransform` updates; validate per-UE minor version API drift.

### 6.3 Navigate the editor UI (“move throughout editor tool”)

**Purpose:** Open assets, focus tabs, switch modes—so the AI can drive the user’s attention.

**Examples of operations:**

- Open **Blueprint** / **Material** / **Level** / **Widget** editor for a given asset path.
- Switch **editor modes** (Place, Landscape, Foliage, etc.) where allowed.
- Focus **Content Browser** on an asset / folder.
- Run safe **menu commands** via `FGlobalTabManager` / `FModuleManager::LoadModuleChecked` patterns with allow-list.

**Requirements:**

- Strict **allow-list** of commands; never arbitrary `Exec` without approval.
- Return **success/failure** + **what UI opened**.

### 6.4 Focus on object in scene (frame / “look at”)

**Purpose:** Move the level viewport to **see** a target object clearly—**the user explicitly asked whether UE5 can do this with size and angle in mind.**

**Behavior spec:**

- Inputs: `Target` (actor reference or object path in level), optional **padding**, **minimum screen coverage** (0–1), **pitch/yaw bias**, **orthographic vs perspective**.
- **Framing algorithm:**
  - Compute **world-space bounds** (`AActor::GetComponentsBoundingBox` with optional-only-colliding flag).
  - Expand by **margin** (e.g. 1.15×) and compute a camera position that fits bounds in view using **FOV** and **aspect ratio** (same idea as “Frame Selection” in editor).
  - If multiple candidates, pick **closest** camera location that satisfies coverage, with **collision** optional raycast to avoid spawning inside geometry.
- **Alignment helpers:** optionally match editor **“Focus Selected”** / **“Snap View to Actor”** behavior for familiarity.

**UE5 implementation notes:**

- Editor already exposes **focus** behaviors (e.g. framing selection). Prefer **calling official editor viewport framing** helpers where stable across versions (search engine code for `MoveViewportCamerasToActor`, `FocusViewportOnBoundingBox`, `EditorViewportClient` framing helpers—the exact symbols vary by minor version).
- If no stable public API, ship a **small internal utility** using bounding sphere + `LookAt` + distance from FOV math (documented and unit-tested).

**Failure cases:**

- Actor not in current world, hidden layer, invalid bounds (infinite), or viewport not available.

---

## 7. Core AI & tooling (asset/graph operations)

*(Parity with reference marketing + docs; enumerate as product backlog.)*

- **Streaming chat** with **interrupt**, **retry**, **clear conversation**.
- **Tool calls** with **arguments**, **permission prompts** for destructive ops.
- **Blueprint**: components, variables, functions, graphs, replication where applicable.
- **Materials**, **Niagara**, **Sequencer**, **BT/ST**, **Enhanced Input**, **UMG**, **PCG**, **MetaSounds**, **Animation** (montage, pose search), etc.
- **Project indexing** for `@` search and suggestions: **deterministic** (Asset Registry, path/name search, tool-mediated reads)—**no** semantic/vector index in MVP (see `agent-and-tool-requirements.md`).
- **Session model**: connect/disconnect; **profile lock** while session active (reference).

---

## 8. Plugin editor layout — screens, panels, and controls

This section is the **UI inventory** for engineering + design. Styling details are in §9.

### 8.1 Dockable **Agent Chat** window (primary)

**Header bar**

| Control | Behavior |
|---------|----------|
| **Profile dropdown** | Switches tool allow-list + instructions; disabled while session active (reference). |
| **Agent dropdown** | Chooses agent type (Claude Code, OpenRouter, …). |
| **Connect / Disconnect** | Establishes session; shows connection state (§9). |
| **Settings (gear)** | Opens settings panel: profiles **+ New**, tool checkboxes, custom instructions, overrides. |
| **New chat / Clear** | Clears thread (confirm if mid-tool). |
| **Help / Docs** | Optional link to internal help tab. |

**Conversation area**

- **Message list** (user vs assistant vs system).
- **Streaming assistant** message with partial text updates.
- **Tool invocation blocks** (collapsible): tool name, args JSON/table, status, result, errors.
- **Thinking / reasoning** blocks if model exposes them (collapsible; see §9).
- **Permission prompts** (modal or inline): Allow once / Always / Deny.

**Composer (input area)**

- Multiline text input.
- **Send** button + **Enter to send**; **Shift+Enter** newline.
- **@** trigger: asset search dropdown with type filters (reference: `@BP_`, `@M_`, `@DT_`).
- **Attachments strip**: chips for attached assets/nodes; remove chip.
- Optional **voice** later (out of scope unless prioritized).

**Footer / status**

- Version string; **Update available** entry (reference changelog mentions manual check).
- Connection latency / model id (optional).

### 8.2 **Settings** panel (from gear)

Sections:

- **Profiles** list + **+ New** + rename/delete (custom only).
- **Tools** matrix: global toggles + per-profile overrides.
- **Agent-specific** fields (paths, API keys with **masked** display).
- **Environment variables** editor (table).
- **MCP** section if enabled (port, autostart).

### 8.3 **Project Settings** page

Same fields as §5.3 in form layout.

### 8.4 **Onboarding / Quick Start** wizard (optional tab)

Steps mirroring reference Quick Start: open chat → choose agent → connect → send example prompt → show tool call review.

### 8.5 **Help / Documentation** viewer (optional)

Embedded markdown viewer for offline docs (ship `docs/` inside plugin Content or external link).

---

## 9. Visual design & UX specification (chat, streaming, tools, thinking)

Public scraped text does **not** include pixel-perfect design tokens; below is a **binding product spec** our designers can refine. Engineering implements **theme tables** in Slate (`FStyle`, `FSlateColor`, brush assets).

### 9.1 Design principles

- **Readability first** in dim editor environments (dark theme default).
- **Clear separation** between user text, assistant text, system, tools, and errors.
- **Never surprise** the user: destructive actions always **explicit** and **confirmable**.

### 9.2 Layout & density

- **Conversation width:** max ~720 px readable column or fluid with **max line length** ~88–100 characters equivalent.
- **Spacing scale:** 4 / 8 / 12 / 16 px; message padding **12–16 px**.
- **Corner radius:** 8 px bubbles (if bubble style) or **flat** Slack-like rows (choose one theme and stay consistent).

### 9.3 Typography

| Role | Spec |
|------|------|
| **Body** | 13–14 px regular; line height ~1.35. |
| **User messages** | Same; optionally **semibold** name label. |
| **Monospace** | Tool args JSON, logs: **12–13 px** mono font (Consolas / JetBrains Mono). |
| **Section titles** | 11–12 px all-caps **label** for “Tool call”, “Thinking”, “Error”. |

### 9.4 Color tokens (dark theme defaults)

Adjust to brand; document in `Theme.json` analogue.

| Token | Default (dark) | Usage |
|-------|------------------|--------|
| `--bg-canvas` | `#1E1E1E` | Chat background |
| `--bg-elevated` | `#252526` | Bubbles / tool panels |
| `--border-subtle` | `#3C3C3C` | Panel borders |
| `--text-primary` | `#E6E6E6` | Assistant + user text |
| `--text-secondary` | `#B0B0B0` | Timestamps, hints |
| `--accent` | `#3B82F6` | Focus ring, links, Connect button |
| `--user-bubble` | `#2D3A4A` | User message background (if bubbles) |
| `--assistant-bubble` | `#2A2A2A` | Assistant message background |

### 9.5 Chatting (user typing)

- **Input border:** `--border-subtle`; **focus** ring 2 px `--accent` @ 60% opacity.
- **Typing indicator** in composer: subtle **“Draft”** label if unsent changes (optional).
- **@ dropdown:** `--bg-elevated`, highlight row `--accent` @ 20% background.

### 9.6 Assistant streaming response

States:

1. **Receiving stream** — caret or **shimmer** on last line only (avoid whole-message flicker).
2. **Stream complete** — transition shimmer off; show **timestamp** + optional **copy** button.

**Rules:**

- Streaming text uses `--text-primary`; partial code/tool JSON must be **escaped** or rendered in monospace sub-block only when finalized (avoid half-rendered JSON).

### 9.7 Tool call presentation

Each tool invocation is a **card**:

| Element | Style |
|---------|--------|
| **Header** | Row: **tool icon** (16 px) + **tool name** (semibold) + **status pill** |
| **Status: pending** | Pill bg `#3A2F00`, text `#FFD27F`, border `#6B5900` |
| **Status: running** | Pill bg `#003A57`, text `#7FD7FF`, animated **indeterminate** progress (2 px bar) |
| **Status: success** | Pill bg `#0F3D1F`, text `#8FF0A0` |
| **Status: error** | Pill bg `#3D1515`, text `#FF9B9B` |
| **Body** | Args in **collapsible** mono block; default **collapsed** if large; **Expand** chevron. |
| **Footer** | “View logs” link → opens audit line filter for this `tool_call_id`. |

**Tool color coding by category (optional second axis):**

| Category | Accent stripe (left 3 px) |
|----------|-----------------------------|
| **Read-only** | `#6B7280` |
| **Asset mutation** | `#F59E0B` |
| **Destructive** | `#EF4444` |
| **Editor navigation** | `#8B5CF6` |
| **Viewport / camera** | `#22C55E` |

### 9.8 “AI is thinking” / reasoning

Support **collapsible** “Thinking” blocks separate from final answer (when model provides chain-of-thought or server sends `reasoning` channels).

| Element | Style |
|---------|--------|
| **Container** | `--bg-elevated` with **dashed** border `#4B5563` |
| **Label** | “Thinking…” with **animated dots** (CSS-like: opacity pulse 1.0 → 0.4 → 1.0, 1.2 s loop) |
| **Content** | Smaller text `--text-secondary`; monospace if structured |
| **Collapsed default** | Yes, if longer than **4 lines** |

**No thinking channel:** show a **single-line** status under header:

> **Assistant is working…** (spinner 16 px, color `--accent`)

Spinner uses **Slate circular progress** or rotating arc; **do not** block UI thread.

### 9.9 Errors, warnings, permissions

- **Inline error** banner: bg `#3D1515`, text `#FECACA`, icon ⚠.
- **Permission dialog** (modal): bright title, bullet summary of action, **Estimated impact** line (e.g. “Edits 1 Blueprint, 3 nodes”).

### 9.10 Accessibility & localization

- Minimum contrast **WCAG AA** for text vs background on all tokens.
- **IME-safe** input (reference changelog mentions IME fixes).
- Japanese / CJK fonts: use engine **international** font fallback for transcript-heavy users.

---

## 10. Security, privacy, compliance

- **Local-first**: chats and settings remain on disk under §2.5; **no mandatory** telemetry or account.
- API keys stored with **OS credential manager** where possible; encrypted blob + DPAPI/Keychain fallback; never log secrets in verbose mode without redaction.
- User opt-in for **telemetry** (off by default); if enabled, must be **anonymous** and **no chat content**.
- Clear **data flow** diagram: **what stays local** (always: history, settings, audit logs) vs **what may go to a user-configured external LLM endpoint** (prompts/responses only when the user connects BYOK).
- **Threat model**: other apps on the same machine can read plaintext if OS permissions are weak—document that full-disk encryption and OS user accounts protect the machine; optional **folder ACL** hardening on Windows.

---

## 11. Observability & support

- **Tool audit log** (see §5.7).
- **Verbose protocol logging** toggle (reference).
- **Export diagnostics** bundle (logs + plugin version + engine version).

---

## 12. Roadmap — Phase 2+ improvements (after parity)

- Deeper **multi-user** / **Perforce** / **Git** conflict handling policies.
- **Vision** loops: automatic screenshot + question (“why is this pink?”).
- **Custom tool plugins** (third-party tool registration via module interface).
- **Evaluation harness** for tool correctness on sample projects.

---

## 13. Success metrics

- Time to first successful Blueprint edit from install < **30 min** median (guided).
- Tool call success rate & **user override rate** for permissions.
- Crash-free sessions / editor stability vs baseline.

---

## 14. Open questions

- Exact **framing API** to standardize on per UE minor version for §6.4.
- Whether to ship **MCP** in v1 or stub.
- Which **agents** are tier-1 supported day one.

---

## 15. Appendix — reference UI map (from scraped pages)

Pages present in `docs/aik-editor/pages/**/page.md`:

- `home`
- `getting-started/installation`, `configuration`, `quick-start`
- `agents`, `agents/choosing-an-agent`, `agents/claude-code`, `agents/openrouter`, `agents/codex`, `agents/gemini-cli`, `agents/cursor`, `agents/copilot`
- `chat-interface`
- `profiles`
- `troubleshooting`
- `comparison`
- `changelog`

---

**End of PRD**
