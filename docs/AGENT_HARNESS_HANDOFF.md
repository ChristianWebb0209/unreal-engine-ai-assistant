# Agent handoff: harness testing & iteration (read this first)

**Give this file to any new agent or contributor.** It is the **single entry point** for continuing work on the Unreal AI Editor **testing harness**, **prompts**, **tool catalog**, and **dispatch**. Other docs go deeper on narrow topics; this file ties them together and tells you **what to do next** and **when to stop and report**.

**Index of all `docs/` files:** [docs/README.md](./README.md).

---

## What you are validating

| Layer | What “good” looks like |
|--------|-------------------------|
| **Prompts** (`Plugins/UnrealAiEditor/prompts/chunks/*.md`) | System/developer instructions match real tool behavior, naming (`snake_case` ids), and safety. Chunks are assembled in `UnrealAiPromptBuilder.cpp`. |
| **Tool catalog** (`Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json`) | Each `tool_id` has accurate `summary`, JSON Schema `parameters`, and `modes` flags so the model knows when a tool applies. |
| **Dispatch** (`UnrealAiToolDispatch*.cpp`) | Every catalog tool either runs, returns structured `not_implemented`, or validates input with clear JSON errors. |
| **Harness** | Same code path as Agent Chat: LLM transport → tool execution → persistence. Artifacts under `Saved/UnrealAiEditor/HarnessRuns/<timestamp>/run.jsonl`. |

**Iteration order (usual):** adjust **prompts** and **catalog text** when the model mis-routes or misunderstands; fix **dispatch** when behavior or validation is wrong; add **`tests/fixtures/<tool_id>.json`** when the matrix should exercise non-empty args. Rebuild **after C++ changes**; JSON-only catalog/fixture changes can be re-tested without a full compile if you only re-run automation that reads files from disk (still safest to build after any doubt).

---

## Three harness tiers (when to use which)

| Tier | Purpose | Typical commands | LLM | Notes |
|------|---------|------------------|-----|--------|
| **1a — CI / contract** | Catalog matrix, tool dispatch, headless automation | `.\build-editor.ps1 -AutomationTests -Headless -SkipBlueprintFormatterSync` | N/A or fixture | Fast, deterministic where tests allow. |
| **1b — Fixture harness** | Same harness as chat, deterministic LLM | `.\scripts\run-headed-scenario-smoke.ps1` (sets `UNREAL_AI_LLM_FIXTURE`) | **Fixture** | Structural checks, `assert_harness_run.py`, no API key burn. |
| **2 — Live headed qualitative** | Real API, tool-goals-style prompts, artifacts for review | `.\scripts\run-headed-live-scenarios.ps1` — [LIVE_HARNESS.md](./LIVE_HARNESS.md) | **Real API** | No `UNREAL_AI_LLM_FIXTURE`; optional `UNREAL_AI_HARNESS_DUMP_CONTEXT`; `tests/bundle_live_harness_review.py` packs `review.md`. |
| **2b — Context workflows** | Multi-turn **same thread**, context dumps / diffs for context manager review | `.\scripts\run-headed-context-workflows.ps1` — [CONTEXT_HARNESS.md](./CONTEXT_HARNESS.md) | **Real API** (or `-AllowFixture`) | `tests/context_workflows/` manifests; `tests/bundle_context_workflow_review.py`; optional `UnrealAi.DumpContextWindow` for snapshots without LLM. |

Tiers **1a/1b** stay in automation loops; **tiers 2 / 2b** are for qualitative passes, cost/time tradeoffs, and context evolution—not CI gates.

---

## Fresh start (no stale context)

Use this when you want a **clean** harness or chat thread:

1. **New editor process** — restart Unreal so `FUnrealAiBackendRegistry` and the LLM transport match your env (required after changing `UNREAL_AI_LLM_FIXTURE`).
2. **Fixture-driven LLM** — set `UNREAL_AI_LLM_FIXTURE` **before** launching the editor (paths relative to project dir are OK). See [AGENT_HARNESS_TESTING.md](./AGENT_HARNESS_TESTING.md#deterministic-llm-fixture-transport). Clear the variable for a real API key run.
3. **New conversation thread** — omit `ThreadGuid` on `UnrealAi.RunAgentTurn` to get a new id, or delete/ignore old `%LOCALAPPDATA%\UnrealAiEditor\` threads if you are debugging persistence (only when relevant).
4. **Automation** — `UnrealEditor-Cmd` / `ExecCmds` runs are one-shot; each run starts clean unless you reuse paths on purpose.

---

## Commands & scripts (cheat sheet)

| Goal | Command |
|------|---------|
| **Build** BlankEditor + plugin | `.\build-editor.ps1 -Headless` — if DLL locked: `.\build-editor.ps1 -Restart -Headless` |
| **Formatter git fails** | `.\build-editor.ps1 -SkipBlueprintFormatterSync` or `UE_SKIP_BLUEPRINT_FORMATTER_SYNC=1` — see [UnrealBlueprintFormatter-dependency.md](./UnrealBlueprintFormatter-dependency.md) |
| **Headless automation (CI-style)** | `.\build-editor.ps1 -AutomationTests -Headless -SkipBlueprintFormatterSync` — runs `UnrealAiEditor.Tools` |
| **Headed smoke: matrix + two harness turns + assert** | `.\build-editor.ps1 -HeadedScenarioSmoke -SkipBlueprintFormatterSync` (runs [`scripts/run-headed-scenario-smoke.ps1`](../scripts/run-headed-scenario-smoke.ps1)) |
| **Same headed script directly** | `.\scripts\run-headed-scenario-smoke.ps1` — optional `-MatrixFilter blueprint`, `-SkipCatalogMatrix` |
| **Live headed qualitative (real API, manifest)** | [`scripts/run-headed-live-scenarios.ps1`](../scripts/run-headed-live-scenarios.ps1) — see [LIVE_HARNESS.md](./LIVE_HARNESS.md); then `python tests\bundle_live_harness_review.py tests\out\live_runs\<suite>` |
| **Context workflows (multi-turn thread, context review)** | [`scripts/run-headed-context-workflows.ps1`](../scripts/run-headed-context-workflows.ps1) — [CONTEXT_HARNESS.md](./CONTEXT_HARNESS.md); `python tests\bundle_context_workflow_review.py tests\out\context_runs\<suite>\<workflow>` |
| **Full test runner (matrix, logs, headed option)** | `.\run-unreal-ai-tests.ps1` — see [tests/README.md](../tests/README.md) |
| **Assert last / specific JSONL** | `python tests\assert_harness_run.py Saved\UnrealAiEditor\HarnessRuns\<ts>\run.jsonl --expect-tool <id> --require-success` |

**PowerShell trap:** `-ExecCmds=Automation RunTests …;Quit` must be **one argv token** (see `build-editor.ps1` splatting). Wrong splitting breaks automation silently.

**In-editor console** (Output Log): `UnrealAi.RunCatalogMatrix [filter]`, `UnrealAi.RunAgentTurn <utf8-msg-file> [thread] [mode] [outdir] [dumpcontext|nodump]` (env `UNREAL_AI_HARNESS_DUMP_CONTEXT=1`), `UnrealAi.DumpContextWindow <ThreadGuid> [reason]`. Details: [AGENT_HARNESS_TESTING.md](./AGENT_HARNESS_TESTING.md).

---

## File map (where to edit)

| Area | Path |
|------|------|
| System prompt chunks | `Plugins/UnrealAiEditor/prompts/chunks/` — order in `UnrealAiPromptBuilder.cpp` |
| Tool definitions | `Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json` |
| Tool implementation | `Plugins/UnrealAiEditor/Source/.../Tools/UnrealAiToolDispatch*.cpp` |
| Matrix fixtures | `tests/fixtures/<tool_id>.json` |
| Headless prompt routing test | `tests/tool-call-prompts.generated.json` (prompt text must **contain** the tool id substring — see `UnrealAiHeadlessToolPromptTests.cpp`) |
| LLM fixture (deterministic) | `tests/harness_llm_fixture.example.json`, schema `tests/harness_llm_fixture.schema.json` |
| Headed scenario messages + multi-turn fixture | `tests/harness_scenarios/` (`user_scenario_*.txt`, `fixture_two_agent_turns.json`) — each `responses[]` entry is consumed per **LLM round** (tool rounds need multiple entries). |
| Qualitative goals | `docs/tool-goals.md` |

---

## What to iterate (in order)

1. **Prompts** — tighten `04-tool-calling-contract.md`, `10-mvp-gameplay-and-tooling.md`, `05-context-and-editor.md`, etc., when the model skips tools, wrong order, or ignores formatter/merge_policy. Keep chunks **consistent** with catalog field names.
2. **Catalog** — improve `summary` and `parameters` descriptions; fix `modes` if Ask/Agent should differ.
3. **Dispatch** — normalize args, return structured errors, fix crashes. Match **C++** to catalog (`tool_id`).
4. **Fixtures** — add matrix fixtures for tools that need non-`{}` args to be meaningful.
5. **Harness fixtures** — extend `responses[]` when you add multi-round scenarios; **each** `StreamChatCompletion` consumes the **next** response object.

---

## When to report back (escalate to humans)

Do **not** only loop silently forever. Stop and summarize when:

- **Engine crashes**, **asserts**, **link errors** you cannot fix in one pass, or **CI/environment** issues (wrong UE path, git lock on formatter).
- **Design decisions**: removing a tool from the catalog, changing security boundaries, or breaking API for existing users.
- **Large refactors** — e.g. splitting the harness, new transport, or restructuring the whole catalog.

**Tool catalog proposals:** If a workflow in `docs/tool-goals.md` **cannot** be achieved with a composition of existing tools (or a small extension of dispatch), **write a short note**: proposed `tool_id`, purpose, parameters, and why current tools are insufficient. Same for **removing** a tool: who depends on it, migration path.

---

## Deeper reference (not duplicated here)

| Doc | Contents |
|-----|----------|
| [AGENT_HARNESS_TESTING.md](./AGENT_HARNESS_TESTING.md) | Artifacts, `UnrealAi.RunAgentTurn`, fixture schema, `assert_harness_run.py`, ExecCmds |
| [LIVE_HARNESS.md](./LIVE_HARNESS.md) | Live headed tier: real API, `run-headed-live-scenarios.ps1`, context dumps, review bundle |
| [CONTEXT_HARNESS.md](./CONTEXT_HARNESS.md) | Multi-turn context workflows, `run-headed-context-workflows.ps1`, `bundle_context_workflow_review.py`, `DumpContextWindow` |
| [tests/TOOL_ITERATION_AGENT_PROMPT.md](../tests/TOOL_ITERATION_AGENT_PROMPT.md) | Matrix iteration loop, log reading, `run-unreal-ai-tests.ps1` |
| [tests/README.md](../tests/README.md) | Test outputs, matrix filters, headed automation |
| [tool-goals.md](./tool-goals.md) | MVP gameplay tasks vs catalog |
| [UnrealBlueprintFormatter-dependency.md](./UnrealBlueprintFormatter-dependency.md) | Formatter plugin sync |

---

## Repo rules reminder

- **UE 5.7** syntax and targets. Use `./build-editor.ps1` as the project standard build entry.
- Run **`.\build-editor.ps1 -Headless`** periodically to catch compile errors; **`-Restart`** when you hit LNK1104 on the plugin DLL.

After you read this file, your next actions should be: **run the relevant script** (headless automation and/or headed scenario smoke), **inspect outputs** (`run.jsonl`, `tool_matrix_last.json`, or `tests/out/*`), then **edit prompts/catalog/dispatch** and repeat.
