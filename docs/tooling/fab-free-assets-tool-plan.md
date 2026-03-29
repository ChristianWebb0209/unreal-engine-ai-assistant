## Fab Free Assets Tool — plan & implementation steps

### Scope and constraints
- **Goal**: Add an editor feature that helps users **authenticate to Fab**, **search/browse free assets**, and **install them into the open Unreal project**.
- **Key constraints**
  - **Fail safe**: if Fab access isn’t available in the user’s local environment (missing browser runtime, plugin disabled, blocked network, login failure, etc.), the tool must **degrade cleanly** (no crashes, no partial installs) and provide **actionable remediation**.
  - **No secrets leakage**: never log raw cookies, auth codes, or tokens. Persist as little as possible.
  - **Terms / guidelines**: Fab developers gave explicit permission provided we follow specific guidelines. This plan assumes we will implement those rules as **hard constraints** (rate limits, user-driven actions, no bypassing paywalls, etc.). Add the final guidelines into this document once received.

### Proposed user experience (editor)
- **Entry point**: `Window → Unreal AI → Fab Free Assets` (new nomad tab).
- **Layout**
  - **Left**: “Fab Browser” panel (embedded web view).
  - **Right**: “Automation” panel (buttons + status + logs): *Login status*, *Search*, *Install*, *Queue*, *Recent installs*.
  - **Bottom**: a collapsible **audit log** for every automated action (timestamped; redacts secrets).
- **Interaction model**
  - **User always sees the webpage** as the tool navigates/searches.
  - Potentially destructive steps (downloading binaries, writing to project content, enabling plugins, importing assets) are **explicit** and (optionally) require a click-to-confirm the first time.

### Architecture overview (inside the plugin)
We’ll treat this as a “browser-backed tool” that uses a real web session and controlled JavaScript automation.

- **Module additions (Editor)**
  - **UI Tab**: `SFabFreeAssetsTab` (Slate), registered alongside existing Unreal AI tabs.
  - **Controller/Service**: `FFabAutomationService`
    - Owns the browser window/widget
    - Tracks state machine (NotReady → Ready → LoggedIn → Searching → Installing)
    - Implements safe-fail checks and user-facing error states
  - **Installer**: `FFabAssetInstaller`
    - Receives a “downloaded payload” (zip/folder) and installs/imports into the project using editor APIs
    - Runs work off the game thread where safe, but marshals Unreal asset creation/import back onto the game thread
  - **Audit**: `FFabAutomationAuditLog`
    - Writes structured entries; redacts sensitive data; supports exporting diagnostics bundle

### “Fail safe” definition (what must be true to run)
The feature is “available” only if all of the following pass; otherwise the UI shows a disabled state with a **single actionable fix** per failure.

- **Browser runtime available**
  - If using UE `WebBrowser` plugin: ensure it’s enabled and the browser subsystem can create a window.
  - If missing, show: “Enable WebBrowser plugin and restart the editor.”
- **Network reachability**
  - DNS + HTTPS to Fab domain(s) succeeds (simple HEAD/GET to a known public URL).
  - If blocked, show: “Network blocked (proxy/firewall). Configure proxy or allow access.”
- **User session**
  - If not logged in, show “Log in to Fab” and keep automation disabled until login is detected.
- **Write permissions**
  - Verify we can write to `Content/` (and optionally `Plugins/`) in the current project, and we have enough disk space for downloads + extraction.
- **Integrity & rollback**
  - All installs must be transactional:
    - Download to a temp directory under `Saved/UnrealAiEditor/Fab/Downloads/<id>/`
    - Validate payload
    - Extract to staging
    - Import/Move into final destination
    - If anything fails: delete staging; keep temp payload only if user opts-in for debugging

### Web embedding decision: UE WebBrowser widget vs bundling Chromium
This is the most important architectural choice. The default recommendation is to start with UE’s WebBrowser/CEF stack and only consider “custom Chromium” if a hard requirement forces it.

#### Option A — UE WebBrowser widget (CEF-based)
- **What it is**: UE’s built-in WebBrowser plugin (CEF under the hood) presented via UMG `WebBrowser` widget or directly in Slate.
- **Pros**
  - **Fastest time-to-first-iteration**: minimal extra build tooling; stays aligned with engine distribution.
  - **Better compatibility with Unreal**: fewer custom binaries; fewer packaging/signing issues.
  - **Editor-friendly**: integrated with editor lifecycle, input, focus, docking.
- **Cons / risks**
  - **API surface limits**: depending on UE version, JS-to-native bridging capabilities may be limited or require engine-private hooks.
  - **CEF version lag**: engine’s CEF may be behind current Chromium; some modern web features might break.
  - **Hard-to-debug web issues**: fewer devtools affordances than a full embedded Chromium build unless we add them.
- **When to choose**
  - We can complete login detection, DOM automation, and downloads within the constraints of UE’s WebBrowser APIs.

#### Option B — “Bring our own Chromium” (custom CEF/Chromium embedding)
- **What it is**: shipping a separate embedded Chromium/CEF runtime as part of the plugin, and hosting it in a custom Slate widget.
- **Pros**
  - **Full control**: modern Chromium version, devtools, richer automation hooks, potentially more robust JS/native messaging.
  - **Consistency**: avoids UE’s CEF variability across engine versions.
- **Cons / risks**
  - **Large distribution footprint**: heavy binaries; slower installs; higher storage.
  - **Maintenance burden**: updating Chromium, security patches, platform differences, signing.
  - **Build complexity**: packaging for Win/Mac; CI complexity; antivirus false positives risk.
- **When to choose**
  - Fab’s site requires modern features missing in UE’s CEF and we cannot work around it.
  - We need automation primitives that UE WebBrowser cannot provide reliably (e.g., robust cookie/session introspection, stable downloads interception, devtools protocol).

#### Decision recommendation (initial)
- **Start with Option A (UE WebBrowser)** for the first milestone.
- Define **hard “escape hatches”** that would trigger Option B:
  - Cannot reliably detect login state.
  - Cannot execute/observe JS automation safely.
  - Downloads cannot be routed to our controlled staging area.
  - Site functionality is broken due to engine CEF limitations.

### Auth & login plan (Fab account)
We should avoid implementing a separate auth flow if the website already provides it.

- **Primary approach**: **in-webview login**
  - Navigate to Fab login page.
  - User logs in normally inside the embedded browser.
  - We detect “logged in” via one or more signals:
    - URL change to a known post-login page
    - Presence of a known DOM element (account avatar, logout button)
    - A “whoami/me” page loads successfully
- **Storage**
  - Prefer **session-only** storage: keep cookies only inside the browser runtime.
  - If “remember me” is desired, store browser data in a dedicated local folder under `%LOCALAPPDATA%\UnrealAiEditor\FabBrowserProfile\` (or equivalent), and document it clearly.
- **Redaction**
  - Never print cookies/localStorage in logs.
  - If we must persist a token-like value, store it via OS-protected storage (DPAPI on Windows) and keep it scoped to the current OS user.

### Automation approach (JavaScript)
The automation layer should be explicit, testable, and guarded by allow-lists.

- **Execution primitive**: “run JS snippet” in the active page (e.g., `ExecuteJavascript`-style call).
- **Result channel**: implement a robust native callback channel such as:
  - “console-message protocol” (JS `console.log("UE_FAB_AUTOMATION:...")` → native OnConsoleMessage)
  - “location hash protocol” (JS sets `window.location.hash` with encoded payload → native OnUrlChanged)
  - “message bridge” (if UE supports binding an object/function callable from JS)
- **Safety guardrails**
  - Only run scripts from our plugin (no user-provided arbitrary JS by default).
  - Include a per-run **timeout** and a **cancel** button.
  - Rate-limit actions (clicks, queries, page loads) and backoff on errors.

### Search & scrape plan (free assets)
The tool should target a narrow, deterministic subset:
- **Free assets only**, based on Fab’s UI filters and/or a dedicated “free” channel/listing page.
- **Metadata collection** (minimal)
  - Asset title, publisher, Fab listing URL, category, file/engine compatibility (if visible), and a stable identifier.

Suggested workflow:
1. Navigate to a “search” page.
2. Apply filters for “Free” and Unreal Engine-compatible formats where available.
3. Extract the top \(N\) results (configurable), show them in a native list.
4. For a selected asset, open the listing page and confirm availability/free status.

### Download & install plan
We must make installation robust and reversible.

- **Download**
  - Prefer using the website’s own download flow inside the webview.
  - Intercept or observe the final artifact URL and download via `HTTP` module into a staging directory (so we control file paths).
  - If we cannot intercept the download, fall back to a user-selected download folder and a “Pick downloaded file” step.
- **Validation**
  - Validate file type (zip, uasset pack, plugin pack, etc.).
  - Validate that the content matches “free asset” expectations (no executables unless explicitly permitted).
- **Extraction**
  - Extract into staging, scan for Unreal asset structures.
- **Install**
  - If it’s content-only: import/copy into `Content/Fab/<AssetName>/`.
  - If it’s a plugin: offer to install under `Plugins/Fab/<PluginName>/` and require an explicit confirmation (and explain editor restart requirements).
  - Run Unreal asset registry refresh and optionally open the installed content folder in Content Browser.
- **Post-install checks**
  - Ensure asset registry sees the new assets.
  - If applicable, run a lightweight validation (e.g., verify expected uassets exist; avoid heavy compile by default).

### Implementation milestones (step-by-step)
#### Milestone 0 — Requirements & policy lock-in
- Collect the Fab-provided guidelines and translate them into:
  - hard-coded rate limits
  - explicit UI disclosures
  - “automation allowed actions” list
  - data retention policy

#### Milestone 1 — Embedded browser tab (no automation)
- Add a new editor tab with embedded browser pointed at Fab.
- Add “environment readiness” checks and safe-fail UI states.
- Add minimal audit logging scaffolding.

#### Milestone 2 — Login detection + session state
- Detect logged-in status reliably.
- Add “Log in” / “Log out” UX.
- Add “Reset browser profile” button (clears local browser storage for Fab profile).

#### Milestone 3 — JS bridge + deterministic scraping
- Implement the JS/native result channel.
- Implement “Search free assets” action:
  - apply filters
  - scrape top \(N\)
  - render in native list
- Add timeouts, cancel, and rate limiting.

#### Milestone 4 — Download staging + install pipeline
- Implement controlled download into staging.
- Implement extraction + install into project content.
- Add rollback semantics and clear error messages.

#### Milestone 5 — Hardening + tests
- Add automated editor tests for:
  - readiness checks
  - install staging/rollback behavior
  - path sanitization and destination rules
- Add “headless build smoke” checks for compilation regressions.

### Security, privacy, compliance checklist
- **Disclosure**: UI text explaining the tool uses an embedded browser and automation to help install free assets; the user explicitly initiates actions.
- **Rate limit**: enforce a conservative request/click cadence; never brute force.
- **Robust redaction**: cookies/tokens not logged; listing URLs ok.
- **No privilege escalation**: never write outside the project unless explicitly confirmed (and never to engine install dirs).
- **Opt-out**: feature flag to disable Fab automation entirely.

### Open questions (to resolve before coding)
- What exact Fab guidelines were provided (rate limits, endpoints, required user gesture, allowed scraping methods)?
- Does Fab provide any official API endpoints for search/listing metadata and download links that we can use instead of DOM scraping?
- What formats do “free” assets typically download as (content pack vs plugin vs zip with multiple variants)?
- Do we need multi-platform support (Win64 editor only for v1 vs Win+Mac)?

