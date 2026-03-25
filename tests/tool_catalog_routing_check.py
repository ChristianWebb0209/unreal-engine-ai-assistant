#!/usr/bin/env python3
"""
1) Static: each catalog tool_id is included in the OpenAI tools surface for its mode
   (FUnrealAiToolCatalog::BuildLlmToolsJsonArrayForMode).

2) Optional --llm: loads API URL + key + default model from the same plugin_settings.json
   the Unreal editor uses (%LOCALAPPDATA%/UnrealAiEditor/settings on Windows), assembles the
   system prompt from prompts/chunks/*.md like UnrealAiPromptBuilder::BuildSystemDeveloperContent,
   and calls the provider chat/completions endpoint to confirm tool_calls include the expected tool.

Python 3.9+. Stdlib only.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def plugin_settings_path() -> Path:
    if sys.platform == "win32":
        base = os.environ.get("LOCALAPPDATA", "")
        if not base:
            base = str(Path.home() / "AppData/Local")
        return Path(base) / "UnrealAiEditor" / "settings" / "plugin_settings.json"
    return Path.home() / "Library/Application Support/UnrealAiEditor/settings/plugin_settings.json"


def engine_version_label(repo_root: Path) -> str:
    for uproj in repo_root.glob("*.uproject"):
        try:
            data = json.loads(uproj.read_text(encoding="utf-8-sig"))
            eng = data.get("EngineAssociation")
            if isinstance(eng, str) and eng:
                return f"Unreal Engine {eng}"
        except (OSError, json.JSONDecodeError):
            pass
    return "Unreal Engine 5.7"


def default_catalog_path(root: Path) -> Path:
    return root / "Plugins" / "UnrealAiEditor" / "Resources" / "UnrealAiToolCatalog.json"


def chunks_dir(root: Path) -> Path:
    return root / "Plugins" / "UnrealAiEditor" / "prompts" / "chunks"


def tool_included_for_mode(modes_obj: dict[str, Any] | None, mode: str) -> bool:
    if not modes_obj:
        return False
    m = mode.lower()
    if m == "ask":
        return bool(modes_obj.get("ask"))
    if m == "agent":
        if bool(modes_obj.get("agent")):
            return True
        return bool(modes_obj.get("fast"))
    if m == "orchestrate":
        if bool(modes_obj.get("orchestrate")):
            return True
        return bool(modes_obj.get("agent"))
    return False


def build_tool_ids_for_mode(catalog: dict[str, Any], mode: str, supports_native_tools: bool) -> set[str]:
    if not supports_native_tools:
        return set()
    tools = catalog.get("tools")
    if not isinstance(tools, list):
        return set()
    out: set[str] = set()
    for entry in tools:
        if not isinstance(entry, dict):
            continue
        tid = entry.get("tool_id")
        if not tid or not isinstance(tid, str):
            continue
        modes = entry.get("modes")
        modes_obj = modes if isinstance(modes, dict) else None
        if tool_included_for_mode(modes_obj, mode):
            out.add(tid)
    return out


def build_openai_tools_array(
    catalog: dict[str, Any], mode: str, supports_native_tools: bool
) -> list[dict[str, Any]]:
    """Mirror FUnrealAiToolCatalog::BuildLlmToolsJsonArrayForMode (JSON array content)."""
    if not supports_native_tools:
        return []
    tools = catalog.get("tools")
    if not isinstance(tools, list):
        return []
    tools_out: list[dict[str, Any]] = []
    for entry in tools:
        if not isinstance(entry, dict):
            continue
        modes = entry.get("modes")
        modes_obj = modes if isinstance(modes, dict) else None
        if not tool_included_for_mode(modes_obj, mode):
            continue
        tid = entry.get("tool_id")
        if not tid:
            continue
        summary = entry.get("summary") or ""
        params = entry.get("parameters")
        if not isinstance(params, dict):
            params = {"type": "object"}
        func = {"name": tid, "description": summary, "parameters": params}
        tools_out.append({"type": "function", "function": func})
    return tools_out


def parse_mode(s: str) -> str:
    x = (s or "agent").strip().lower()
    if x in ("ask", "agent", "orchestrate"):
        return x
    return "agent"


def load_fixture_cases(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    cases = data.get("cases")
    if not isinstance(cases, list):
        raise ValueError("Fixture missing 'cases' array")
    out: list[dict[str, Any]] = []
    for c in cases:
        if not isinstance(c, dict):
            continue
        prompt = c.get("prompt") or ""
        mode = parse_mode(str(c.get("mode") or "agent"))
        expected = c.get("expected_tool_calls") or []
        if not isinstance(expected, list):
            expected = []
        ids = [str(x) for x in expected if x]
        if ids:
            out.append({"prompt": prompt, "mode": mode, "expected_tool_calls": ids})
    return out


def generate_cases_from_catalog(catalog: dict[str, Any]) -> list[dict[str, Any]]:
    tools = catalog.get("tools")
    if not isinstance(tools, list):
        return []
    cases: list[dict[str, Any]] = []
    n = 0
    for t in tools:
        if not isinstance(t, dict):
            continue
        tid = t.get("tool_id")
        if not tid:
            continue
        summary = (t.get("summary") or "").strip() or "run the requested action"
        modes = t.get("modes") if isinstance(t.get("modes"), dict) else {}
        mode = "agent"
        if modes.get("agent") or modes.get("fast"):
            mode = "agent"
        elif modes.get("orchestrate"):
            mode = "orchestrate"
        elif modes.get("ask"):
            mode = "ask"
        else:
            continue
        n += 1
        if n == 1:
            prompt = (
                f"Hey, let's start a continuous tools QA chat. First, please call tool '{tid}' "
                f"and use it to {summary}."
            )
        else:
            prompt = "Great, continuing the same thread: now call tool '{tid}' and proceed naturally.".format(
                tid=tid
            )
        cases.append({"prompt": prompt, "mode": mode, "expected_tool_calls": [str(tid)]})
    return cases


def run_static_cases(
    catalog: dict[str, Any],
    cases: list[dict[str, Any]],
    *,
    supports_native_tools: bool,
) -> tuple[int, list[str]]:
    failures = 0
    errors: list[str] = []
    for c in cases:
        mode = c["mode"]
        prompt: str = c["prompt"]
        available = build_tool_ids_for_mode(catalog, mode, supports_native_tools)
        for tool_id in c["expected_tool_calls"]:
            if tool_id not in available:
                failures += 1
                errors.append(
                    f"Tool '{tool_id}' not in OpenAI tools set for mode '{mode}' "
                    f"(check modes.* in catalog). prompt={prompt[:80]!r}..."
                )
                continue
            if tool_id.lower() not in prompt.lower():
                failures += 1
                errors.append(
                    f"Fixture prompt must mention tool id '{tool_id}' for this check. prompt={prompt[:80]!r}..."
                )
    return failures, errors


# --- Complexity (UnrealAiComplexityAssessor::Assess) ---


def _count_bullet_lines(text: str) -> int:
    n = 0
    for line in text.splitlines():
        t = line.strip()
        if t.startswith(("-", "*", "+")):
            n += 1
            continue
        if len(t) > 2 and t[0].isdigit() and "." in t:
            n += 1
    return n


def assess_complexity(
    user_message: str,
    mode: str,
    *,
    attachment_count: int = 0,
    tool_result_count: int = 0,
    context_block_char_count: int = 0,
    content_browser_selection_count: int = 0,
) -> tuple[str, float, bool, str, list[str]]:
    length = len(user_message)
    bullets = _count_bullet_lines(user_message)
    s = 0.0
    s += max(0.0, min(1.0, length / 4500.0)) * 0.28
    s += max(0.0, min(0.35, attachment_count * 0.09))
    s += max(0.0, min(0.4, tool_result_count * 0.08))
    s += max(0.0, min(1.0, context_block_char_count / 65000.0)) * 0.18
    s += max(0.0, min(0.2, bullets * 0.04))
    s += max(0.0, min(0.15, content_browser_selection_count * 0.05))
    m = mode.lower()
    if m == "agent":
        s += 0.14
    elif m == "orchestrate":
        s += 0.20
    score = max(0.0, min(1.0, s))
    if score < 0.34:
        label = "low"
    elif score < 0.62:
        label = "medium"
    else:
        label = "high"
    signals: list[str] = []
    if length > 900:
        signals.append("long_message")
    if bullets >= 3:
        signals.append("bullets")
    if attachment_count > 0:
        signals.append("attachments")
    if tool_result_count > 0:
        signals.append("tool_memory")
    if content_browser_selection_count > 2:
        signals.append("many_asset_selections")
    if m == "agent":
        signals.append("agent_mode")
    elif m == "orchestrate":
        signals.append("orchestrate_mode")
    else:
        signals.append("ask_mode")
    hard_multi = length > 1600 or bullets >= 5 or tool_result_count >= 4
    recommend_gate = score >= 0.44 or hard_multi
    sig = "; ".join(signals)
    block = (
        f"[Complexity]\nscore: {score:.2f} ({label})\n"
        f"signals: {sig if sig else '(none)'}\n"
        "policy: if high OR clearly multi-goal, prefer emitting unreal_ai.todo_plan "
        "(via agent_emit_todo_plan) before destructive tools\n"
        f"recommendPlanGate: {str(recommend_gate).lower()}"
    )
    return label, score, recommend_gate, block, signals


# --- Prompt builder (subset of UnrealAiPromptBuilder::BuildSystemDeveloperContent) ---


def _replace_all(s: str, old: str, new: str) -> str:
    return s.replace(old, new)


def agent_mode_string(mode: str) -> str:
    m = mode.lower()
    if m == "ask":
        return "ask"
    if m == "orchestrate":
        return "orchestrate"
    return "agent"


def extract_operating_mode_section(chunk02: str, mode: str) -> str:
    starts = {
        "ask": "## Mode: Ask (`ask`)",
        "agent": "## Mode: Agent (`agent`)",
        "orchestrate": "## Mode: Orchestrate (`orchestrate`)",
    }
    ends = {
        "ask": "## Mode: Agent (`agent`)",
        "agent": "## Mode: Orchestrate (`orchestrate`)",
        "orchestrate": None,
    }
    m = mode.lower()
    if m not in starts:
        m = "agent"
    start_tag = starts[m]
    end_tag = ends[m]
    idx = chunk02.find(start_tag)
    if idx == -1:
        return chunk02
    end_idx = len(chunk02)
    if end_tag:
        found = chunk02.find(end_tag, idx + 1)
        if found != -1:
            end_idx = found
    first_mode = chunk02.find("## Mode:")
    preamble = ""
    if first_mode != -1 and first_mode <= idx:
        preamble = chunk02[:first_mode]
    section = chunk02[idx:end_idx]
    return preamble + section


def load_chunk(chunks_root: Path, name: str) -> str:
    path = chunks_root / name
    if path.is_file():
        return path.read_text(encoding="utf-8-sig")
    return ""


def format_context_block_minimal(engine_label: str) -> str:
    """Empty session: only engine banner (see AgentContextFormat::FormatContextBlock)."""
    return f"### (({engine_label}))\n\n"


def build_system_developer_content(
    repo_root: Path,
    mode: str,
    user_message: str,
    thread_id: str,
    llm_round: int,
    max_llm_rounds: int,
    *,
    b_include_execution_subturn_chunk: bool = False,
) -> str:
    cdir = chunks_dir(repo_root)
    acc_parts: list[str] = []

    def append_chunk(fname: str) -> None:
        c = load_chunk(cdir, fname)
        if c:
            if acc_parts:
                acc_parts.append("\n\n---\n\n")
            acc_parts.append(c)

    append_chunk("01-identity.md")
    c2 = load_chunk(cdir, "02-operating-modes.md")
    if c2:
        c2 = extract_operating_mode_section(c2, mode)
        if acc_parts:
            acc_parts.append("\n\n---\n\n")
        acc_parts.append(c2)
    append_chunk("03-complexity-and-todo-plan.md")
    append_chunk("04-tool-calling-contract.md")
    append_chunk("05-context-and-editor.md")
    if b_include_execution_subturn_chunk:
        append_chunk("06-execution-subturn.md")
    append_chunk("07-safety-banned.md")
    append_chunk("08-output-style.md")
    if mode.lower() == "orchestrate":
        append_chunk("09-orchestration-workers.md")

    acc = "".join(acc_parts)
    ctx_block = format_context_block_minimal(engine_version_label(repo_root))
    _, _, _, complexity_block, _ = assess_complexity(
        user_message,
        mode,
        attachment_count=0,
        tool_result_count=0,
        context_block_char_count=len(ctx_block),
        content_browser_selection_count=0,
    )
    active_todo = "(no active plan on disk)"
    round_str = f"{max(1, llm_round)} / {max(1, max_llm_rounds)}"
    pointer = f"threadId={thread_id or '(unknown)'}; storage=context.json activeTodoPlan + todoStepsDone"

    acc = _replace_all(acc, "{{COMPLEXITY_BLOCK}}", complexity_block)
    acc = _replace_all(acc, "{{CONTEXT_SERVICE_OUTPUT}}", ctx_block)
    acc = _replace_all(acc, "{{AGENT_MODE}}", agent_mode_string(mode))
    acc = _replace_all(acc, "{{MAX_PLAN_STEPS}}", "12")
    acc = _replace_all(acc, "{{ACTIVE_TODO_SUMMARY}}", active_todo)
    acc = _replace_all(acc, "{{CONTINUATION_ROUND}}", round_str)
    acc = _replace_all(acc, "{{PLAN_POINTER}}", pointer)
    acc = _replace_all(acc, "((project version))", engine_version_label(repo_root))

    if not acc.strip():
        return (
            "You are an AI assistant embedded in the Unreal Editor. Follow the user's instructions, "
            "respect tool and safety policies, and prefer read-only tools before writes."
        )
    return acc


# --- plugin_settings.json → API (FUnrealAiModelProfileRegistry::TryResolveApiForModel) ---


def _parse_settings_providers(settings: dict[str, Any]) -> tuple[str, str, str, dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    """default_model_id, global_base, global_key, providers_by_id, model_caps_by_profile_key (partial)."""
    api = settings.get("api") if isinstance(settings.get("api"), dict) else {}
    default_model = str(api.get("defaultModel") or "openai/gpt-4o-mini")
    global_base = str(api.get("baseUrl") or "https://openrouter.ai/api/v1")
    global_key = str(api.get("apiKey") or "")
    providers: dict[str, dict[str, Any]] = {}
    models: dict[str, dict[str, Any]] = {}

    sections = settings.get("sections")
    if isinstance(sections, list):
        for sv in sections:
            if not isinstance(sv, dict):
                continue
            sid = str(sv.get("id") or "")
            if not sid:
                continue
            providers[sid] = {
                "baseUrl": str(sv.get("baseUrl") or ""),
                "apiKey": str(sv.get("apiKey") or ""),
            }
            for mo in sv.get("models") or []:
                if not isinstance(mo, dict):
                    continue
                pk = str(mo.get("profileKey") or "")
                if not pk:
                    continue
                caps = {
                    "modelIdForApi": str(mo.get("modelIdForApi") or pk or ""),
                    "supportsNativeTools": bool(mo.get("supportsNativeTools", True)),
                    "providerId": sid,
                }
                models[pk] = caps

    return default_model, global_base, global_key, providers, models


def resolve_api_for_model(settings: dict[str, Any], model_profile_id: str) -> tuple[str, str] | None:
    dm, g_base, g_key, prov_by_id, model_map = _parse_settings_providers(settings)
    key = model_profile_id or dm
    cap = model_map.get(key) or {}
    provider_to_use = str(cap.get("providerId") or "")
    api = settings.get("api") if isinstance(settings.get("api"), dict) else {}
    if not provider_to_use:
        provider_to_use = str(api.get("defaultProviderId") or "")

    if provider_to_use and provider_to_use in prov_by_id:
        p = prov_by_id[provider_to_use]
        base = p.get("baseUrl") or g_base
        ak = p.get("apiKey") or g_key
        if base and ak:
            return str(base).rstrip("/"), str(ak)
    base = g_base
    ak = g_key
    if base and ak:
        return str(base).rstrip("/"), str(ak)
    return None


def model_id_for_api(settings: dict[str, Any], model_profile_id: str) -> str:
    dm, _, _, _, model_map = _parse_settings_providers(settings)
    key = model_profile_id or dm
    cap = model_map.get(key)
    if isinstance(cap, dict):
        mid = cap.get("modelIdForApi")
        if mid:
            return str(mid)
    return key


def supports_native_tools_for_profile(settings: dict[str, Any], model_profile_id: str) -> bool:
    dm, _, _, _, model_map = _parse_settings_providers(settings)
    key = model_profile_id or dm
    cap = model_map.get(key)
    if isinstance(cap, dict) and "supportsNativeTools" in cap:
        return bool(cap["supportsNativeTools"])
    return True


def load_plugin_settings() -> dict[str, Any] | None:
    p = plugin_settings_path()
    if not p.is_file():
        return None
    try:
        return json.loads(p.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None


# --- HTTP chat/completions ---


def post_chat_completions(
    base_url: str,
    api_key: str,
    model: str,
    system: str,
    user: str,
    tools: list[dict[str, Any]] | None,
    *,
    tool_choice: str | dict[str, Any] | None = "auto",
    timeout_s: float = 120.0,
) -> dict[str, Any]:
    url = f"{base_url.rstrip('/')}/chat/completions"
    body: dict[str, Any] = {
        "model": model,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
        "temperature": 0,
    }
    if tools:
        body["tools"] = tools
        body["tool_choice"] = tool_choice if tool_choice is not None else "auto"
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout_s) as resp:
        return json.loads(resp.read().decode("utf-8"))


def message_content_from_response(resp: dict[str, Any]) -> str:
    choices = resp.get("choices")
    if not isinstance(choices, list) or not choices:
        return ""
    msg = choices[0].get("message") if isinstance(choices[0], dict) else None
    if not isinstance(msg, dict):
        return ""
    c = msg.get("content")
    return c if isinstance(c, str) else ""


def message_finish_reason(resp: dict[str, Any]) -> str:
    choices = resp.get("choices")
    if not isinstance(choices, list) or not choices:
        return ""
    fr = choices[0].get("finish_reason") if isinstance(choices[0], dict) else None
    return str(fr) if fr else ""


def message_tool_names_from_response(resp: dict[str, Any]) -> list[str]:
    names: list[str] = []
    choices = resp.get("choices")
    if not isinstance(choices, list) or not choices:
        return names
    msg = choices[0].get("message") if isinstance(choices[0], dict) else None
    if not isinstance(msg, dict):
        return names
    tcalls = msg.get("tool_calls")
    if not isinstance(tcalls, list):
        return names
    for tc in tcalls:
        if not isinstance(tc, dict):
            continue
        fn = tc.get("function")
        if isinstance(fn, dict):
            n = fn.get("name")
            if isinstance(n, str) and n:
                names.append(n)
    return names


def _audit(audit: list[str] | None, line: str) -> None:
    if audit is not None:
        audit.append(line)


def run_llm_cases(
    repo_root: Path,
    catalog: dict[str, Any],
    cases: list[dict[str, Any]],
    settings: dict[str, Any],
    *,
    llm_max: int,
    audit: list[str] | None = None,
    force_expected_tool_choice: bool = True,
) -> tuple[int, list[str]]:
    api_block = settings.get("api") if isinstance(settings.get("api"), dict) else {}
    profile = str(api_block.get("defaultModel") or "") or "openai/gpt-4o-mini"
    resolved = resolve_api_for_model(settings, profile)
    if not resolved:
        msg = "Could not resolve API base URL + key (see plugin AI Settings / plugin_settings.json)."
        _audit(audit, f"FATAL: {msg}")
        return 1, [msg]
    base_url, api_key = resolved
    model = model_id_for_api(settings, profile)
    caps_native = supports_native_tools_for_profile(settings, profile)

    settings_file = plugin_settings_path()
    _audit(audit, f"Timestamp (UTC): {time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}")
    _audit(audit, f"Settings file (API key read at runtime; not written to this log): {settings_file}")
    _audit(audit, f"Resolved HTTP base: {base_url}")
    _audit(audit, f"Model id (API): {model}")
    _audit(audit, f"Default profile key: {profile}")
    _audit(audit, f"supportsNativeTools (from profile): {caps_native}")
    _audit(audit, f"llm_max (0 = no cap): {llm_max}")
    _audit(audit, "=" * 80)

    errors: list[str] = []
    failures = 0
    n_done = 0
    thread_id = "headless_constant_chat_thread"

    for case_idx, c in enumerate(cases):
        if llm_max > 0 and n_done >= llm_max:
            _audit(audit, f"\nStopped early: hit llm_max={llm_max} after {n_done} LLM request(s).")
            break
        mode = c["mode"]
        if mode == "orchestrate":
            _audit(
                audit,
                f"\n--- Case {case_idx} SKIP (orchestrate; editor uses empty tools for planner) ---",
            )
            continue
        prompt = c["prompt"]
        expected_list = c["expected_tool_calls"]
        _audit(audit, f"\n--- Case {case_idx} mode={mode} ---")
        _audit(audit, f"Expected tool call(s): {expected_list}")
        _audit(audit, f"User prompt ({len(prompt)} chars):\n{prompt}\n")

        system = build_system_developer_content(
            repo_root,
            mode,
            prompt,
            thread_id,
            llm_round=1,
            max_llm_rounds=16,
        )
        tools_arr = build_openai_tools_array(catalog, mode, caps_native)
        openai_tools: list[dict[str, Any]] | None = tools_arr if tools_arr else None
        _audit(audit, f"Tools sent to API: {len(tools_arr)} function definition(s).")

        tc_choice: str | dict[str, Any] | None = "auto"
        if (
            force_expected_tool_choice
            and expected_list
            and openai_tools
            and expected_list[0]
        ):
            sent_names = {t.get("function", {}).get("name") for t in openai_tools}
            sent_names.discard(None)
            exp0 = expected_list[0]
            if exp0 in sent_names:
                tc_choice = {"type": "function", "function": {"name": exp0}}
                _audit(
                    audit,
                    f"tool_choice: forced OpenAI function {exp0!r} (default LLM mode verifies catalog/schema wiring; "
                    f"use --no-force-tool-choice to test free-form model routing only).",
                )

        try:
            resp = post_chat_completions(
                base_url,
                api_key,
                model,
                system,
                prompt,
                openai_tools,
                tool_choice=tc_choice,
            )
        except urllib.error.HTTPError as e:
            failures += 1
            err_body = e.read().decode("utf-8", errors="replace")[:4000]
            _audit(audit, f"HTTP ERROR {e.code}\n{err_body}\n")
            errors.append(f"Case {case_idx} HTTP {e.code}: {err_body[:500]}")
            n_done += 1
            continue
        except Exception as e:  # noqa: BLE001
            failures += 1
            _audit(audit, f"REQUEST EXCEPTION: {e!r}\n")
            errors.append(f"Case {case_idx} request failed: {e}")
            n_done += 1
            continue

        got = message_tool_names_from_response(resp)
        content = message_content_from_response(resp)
        fr = message_finish_reason(resp)
        _audit(audit, f"finish_reason: {fr}")
        _audit(audit, f"tool_calls (names): {got}")
        if content.strip():
            prev = content.strip().replace("\r\n", "\n")
            if len(prev) > 1200:
                prev = prev[:1200] + "\n... [truncated]"
            _audit(audit, f"assistant content (text):\n{prev}\n")

        case_fail = False
        for exp in expected_list:
            if exp not in got:
                failures += 1
                case_fail = True
                line = (
                    f"MISSING expected tool '{exp}' in model tool_calls "
                    f"(got only {got}) mode={mode} case={case_idx}"
                )
                _audit(audit, f"PROBLEM: {line}")
                errors.append(line)
        if not case_fail:
            _audit(audit, "Result: OK (all expected tools in tool_calls).")
        n_done += 1

    _audit(audit, "\n" + "=" * 80)
    _audit(audit, f"Summary: LLM requests={n_done}, failure rows={failures}")
    return failures, errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Tool catalog routing + optional LLM tool-call check.")
    parser.add_argument("--catalog", type=Path, help="UnrealAiToolCatalog.json")
    parser.add_argument("--fixture", type=Path, help="tool-call-prompts.generated.json")
    parser.add_argument(
        "--no-native-tools",
        action="store_true",
        help="Static check only: simulate supportsNativeTools=false.",
    )
    parser.add_argument(
        "--llm",
        action="store_true",
        help="After static pass: call provider using API key from plugin_settings.json (same path as Unreal).",
    )
    parser.add_argument(
        "--llm-max",
        type=int,
        default=5,
        help="Max LLM cases to run (ask/agent only; orchestrate skipped). 0 = no limit.",
    )
    parser.add_argument(
        "--settings",
        type=Path,
        help="Override path to plugin_settings.json",
    )
    parser.add_argument(
        "--audit-log",
        type=Path,
        help="Append detailed per-case audit (no secrets) to this file.",
    )
    parser.add_argument(
        "--no-force-tool-choice",
        action="store_true",
        help="With --llm: do not force OpenAI tool_choice to the expected tool (behavioral test only; often flaky).",
    )
    args = parser.parse_args()

    root = repo_root_from_script()
    catalog_path = args.catalog or default_catalog_path(root)
    if not catalog_path.is_file():
        print(f"Catalog not found: {catalog_path}", file=sys.stderr)
        return 2

    catalog = json.loads(catalog_path.read_text(encoding="utf-8-sig"))

    if args.fixture and args.fixture.is_file():
        cases = load_fixture_cases(args.fixture)
    else:
        cases = generate_cases_from_catalog(catalog)

    if not cases:
        print("No runnable cases (empty fixture or catalog).", file=sys.stderr)
        return 2

    supports = not args.no_native_tools
    failures, errors = run_static_cases(catalog, cases, supports_native_tools=supports)

    if failures:
        for e in errors:
            print(e, file=sys.stderr)
        print(f"FAILED (static): {failures} check(s)", file=sys.stderr)
        return 1

    print(f"OK (static): {len(cases)} case(s), catalog routing matches C++ BuildLlmToolsJsonArrayForMode.")

    settings: dict[str, Any] | None = None
    if args.settings and args.settings.is_file():
        settings = json.loads(args.settings.read_text(encoding="utf-8-sig"))
    else:
        settings = load_plugin_settings()

    settings_path = args.settings or plugin_settings_path()
    if args.llm:
        if not settings:
            print(f"--llm requires plugin settings at {settings_path}", file=sys.stderr)
            return 2
        r = resolve_api_for_model(settings, str((settings.get("api") or {}).get("defaultModel") or ""))
        if not r:
            print("No API base URL + key in plugin settings.", file=sys.stderr)
            return 2
        max_n = args.llm_max
        audit_lines: list[str] | None = [] if args.audit_log else None
        f2, err2 = run_llm_cases(
            root,
            catalog,
            cases,
            settings,
            llm_max=max_n,
            audit=audit_lines,
            force_expected_tool_choice=not args.no_force_tool_choice,
        )
        if args.audit_log and audit_lines is not None:
            args.audit_log.parent.mkdir(parents=True, exist_ok=True)
            args.audit_log.write_text("\n".join(audit_lines) + "\n", encoding="utf-8")
            print(f"Wrote audit log: {args.audit_log}")
        if f2:
            for e in err2:
                print(e, file=sys.stderr)
            print(f"FAILED (llm): {f2}", file=sys.stderr)
            return 1
        print(
            f"OK (llm): live tool calls matched expected tools "
            f"(prompt chunks + {settings_path.name}; model routing from settings)."
        )
    else:
        if settings and resolve_api_for_model(
            settings, str((settings.get("api") or {}).get("defaultModel") or "")
        ):
            print(
                f"Tip: API key present in {settings_path}; run with --llm to verify tool calls "
                "with the same system prompt as the editor (prompts/chunks)."
            )
        else:
            print(
                f"No usable API key in {settings_path}; --llm skipped (configure in editor AI Settings)."
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
