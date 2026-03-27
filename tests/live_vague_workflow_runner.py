#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from tool_catalog_routing_check import (
    build_system_developer_content,
    default_catalog_path,
    load_plugin_settings,
    model_id_for_api,
    parse_mode,
    repo_root_from_script,
    resolve_api_for_model,
)


@dataclass
class ToolMemory:
    tool: str
    result: str
    source_turn: int
    added_reason: str


@dataclass
class ContextState:
    attachments: list[str] = field(default_factory=list)
    tool_memories: list[ToolMemory] = field(default_factory=list)
    selected_assets: list[str] = field(default_factory=list)
    max_chars: int = 2800

    def render(self, engine_label: str) -> str:
        lines: list[str] = [f"### (({engine_label}))", ""]
        lines.append("attachments:")
        if self.attachments:
            for a in self.attachments:
                lines.append(f"- {a}")
        else:
            lines.append("- (none)")
        lines.append("")
        lines.append("selected_assets:")
        if self.selected_assets:
            for a in self.selected_assets:
                lines.append(f"- {a}")
        else:
            lines.append("- (none)")
        lines.append("")
        lines.append("recent_tool_results:")
        if self.tool_memories:
            for m in self.tool_memories:
                clean = m.result.replace("\n", " ").strip()
                lines.append(f"- [{m.tool}] {clean[:180]}")
        else:
            lines.append("- (none)")
        lines.append("")
        return "\n".join(lines).strip() + "\n"

    def prune_to_budget(self, engine_label: str) -> list[dict[str, Any]]:
        events: list[dict[str, Any]] = []
        while len(self.render(engine_label)) > self.max_chars and self.tool_memories:
            dropped = self.tool_memories.pop(0)
            events.append(
                {
                    "kind": "tool_memory",
                    "dropped_tool": dropped.tool,
                    "source_turn": dropped.source_turn,
                    "reason": "budget_trim_oldest_tool_memory",
                }
            )
        while len(self.render(engine_label)) > self.max_chars and self.attachments:
            dropped = self.attachments.pop(0)
            events.append(
                {
                    "kind": "attachment",
                    "dropped_attachment": dropped,
                    "reason": "budget_trim_oldest_attachment",
                }
            )
        return events


def engine_label(root: Path) -> str:
    for uproj in root.glob("*.uproject"):
        try:
            data = json.loads(uproj.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        eng = data.get("EngineAssociation")
        if isinstance(eng, str) and eng:
            return f"Unreal Engine {eng}"
    return "Unreal Engine 5.7"


def load_scenario(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    turns = data.get("turns")
    if not isinstance(turns, list):
        raise ValueError("Scenario must have turns[]")
    out: list[dict[str, Any]] = []
    for t in turns:
        if not isinstance(t, dict):
            continue
        msg = str(t.get("user") or "").strip()
        if not msg:
            continue
        out.append(
            {
                "mode": parse_mode(str(t.get("mode") or "agent")),
                "user": msg,
                "attachments": [str(x) for x in (t.get("attachments") or []) if x],
                "selected_assets": [str(x) for x in (t.get("selected_assets") or []) if x],
            }
        )
    return out


def build_tool_map(catalog: dict[str, Any], tool_ids: list[str]) -> dict[str, dict[str, Any]]:
    tools = catalog.get("tools")
    out: dict[str, dict[str, Any]] = {}
    if not isinstance(tools, list):
        return out
    wanted = set(tool_ids)
    for t in tools:
        if not isinstance(t, dict):
            continue
        tid = t.get("tool_id")
        if isinstance(tid, str) and tid in wanted:
            out[tid] = t
    return out


def openai_tools_from_map(tool_map: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    arr: list[dict[str, Any]] = []
    for tid, t in tool_map.items():
        summary = str(t.get("summary") or "")
        params = t.get("parameters")
        if not isinstance(params, dict):
            params = {"type": "object"}
        arr.append({"type": "function", "function": {"name": tid, "description": summary, "parameters": params}})
    return arr


def post_chat_completions(
    base_url: str,
    api_key: str,
    model: str,
    messages: list[dict[str, Any]],
    tools: list[dict[str, Any]],
    timeout_s: float = 120.0,
) -> dict[str, Any]:
    body: dict[str, Any] = {"model": model, "messages": messages, "temperature": 0, "tools": tools, "tool_choice": "auto"}
    url = f"{base_url.rstrip('/')}/chat/completions"
    req = urllib.request.Request(
        url,
        data=json.dumps(body).encode("utf-8"),
        method="POST",
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {api_key}"},
    )
    with urllib.request.urlopen(req, timeout=timeout_s) as resp:
        return json.loads(resp.read().decode("utf-8"))


def parse_response(resp: dict[str, Any]) -> tuple[str, list[dict[str, Any]], str]:
    choices = resp.get("choices")
    if not isinstance(choices, list) or not choices:
        return "", [], ""
    c0 = choices[0] if isinstance(choices[0], dict) else {}
    msg = c0.get("message") if isinstance(c0.get("message"), dict) else {}
    content = msg.get("content") if isinstance(msg.get("content"), str) else ""
    finish_reason = str(c0.get("finish_reason") or "")
    tcalls_raw = msg.get("tool_calls") if isinstance(msg.get("tool_calls"), list) else []
    out_calls: list[dict[str, Any]] = []
    for tc in tcalls_raw:
        if not isinstance(tc, dict):
            continue
        fn = tc.get("function") if isinstance(tc.get("function"), dict) else {}
        out_calls.append(
            {
                "id": str(tc.get("id") or ""),
                "name": str(fn.get("name") or ""),
                "arguments_json": str(fn.get("arguments") or "{}"),
            }
        )
    return content, out_calls, finish_reason


def validate_tool_call(tool_map: dict[str, dict[str, Any]], name: str, arguments_json: str) -> dict[str, Any]:
    if name not in tool_map:
        return {"valid": False, "reason": "tool_not_in_offered_set"}
    t = tool_map[name]
    params = t.get("parameters") if isinstance(t.get("parameters"), dict) else {"type": "object"}
    try:
        args = json.loads(arguments_json or "{}")
    except json.JSONDecodeError:
        return {"valid": False, "reason": "arguments_not_json"}
    if not isinstance(args, dict):
        return {"valid": False, "reason": "arguments_not_object"}
    props = params.get("properties") if isinstance(params.get("properties"), dict) else {}
    required = params.get("required") if isinstance(params.get("required"), list) else []
    additional = params.get("additionalProperties")
    missing = [k for k in required if k not in args]
    if missing:
        return {"valid": False, "reason": f"missing_required:{','.join(missing)}"}
    if additional is False:
        unknown = [k for k in args.keys() if k not in props]
        if unknown:
            return {"valid": False, "reason": f"unknown_keys:{','.join(unknown)}"}
    return {"valid": True, "reason": "ok"}


def main() -> int:
    parser = argparse.ArgumentParser(description="Run live vague multi-turn workflow and log context evolution.")
    parser.add_argument("--scenario", type=Path, default=Path("tests/vague_complex_workflow.json"))
    parser.add_argument("--out", type=Path, default=Path("tests/out/live_runs/vague_workflow_run.jsonl"))
    parser.add_argument("--sleep-ms", type=int, default=650)
    parser.add_argument("--context-max-chars", type=int, default=2800)
    args = parser.parse_args()

    root = repo_root_from_script()
    settings = load_plugin_settings()
    if not settings:
        print("Could not load plugin settings for API endpoint.", file=sys.stderr)
        return 2
    api = resolve_api_for_model(settings, str((settings.get("api") or {}).get("defaultModel") or ""))
    if not api:
        print("Could not resolve API base/key from settings.", file=sys.stderr)
        return 2
    base_url, api_key = api
    profile = str((settings.get("api") or {}).get("defaultModel") or "")
    model = model_id_for_api(settings, profile)

    catalog = json.loads(default_catalog_path(root).read_text(encoding="utf-8-sig"))
    scenario = load_scenario(args.scenario)
    if not scenario:
        print("Scenario empty", file=sys.stderr)
        return 2

    requested_tools = [
        "asset_registry_query",
        "contentbrowser_list_selected_assets",
        "contentbrowser_sync_to_assets",
        "blueprint_export_ir",
        "blueprint_compile",
        "blueprint_apply_ir",
        "blueprint_format_graph",
        "material_get_usage_summary",
        "material_instance_set_scalar_parameter",
        "editor_get_selection",
        "scene_list_actors",
        "scene_fuzzy_search",
        "pie_start",
        "pie_status",
        "pie_stop",
        "editor_state_snapshot_read",
        "asset_open_editor",
        "asset_save_packages",
    ]
    tool_map = build_tool_map(catalog, requested_tools)
    tools = openai_tools_from_map(tool_map)
    if not tools:
        print("No tools built for run.", file=sys.stderr)
        return 2

    state = ContextState(max_chars=max(800, args.context_max_chars))
    eng = engine_label(root)
    transcript: list[dict[str, Any]] = []
    history: list[dict[str, Any]] = []
    thread_id = "live_vague_workflow_thread"

    for idx, turn in enumerate(scenario, start=1):
        state.attachments.extend([a for a in turn["attachments"] if a not in state.attachments])
        state.selected_assets = turn["selected_assets"] or state.selected_assets

        pre_prune_events = state.prune_to_budget(eng)
        context_block = state.render(eng)
        sys_base = build_system_developer_content(
            root,
            turn["mode"],
            turn["user"],
            thread_id,
            llm_round=idx,
            max_llm_rounds=len(scenario),
        )
        dynamic_context_system = (
            "## Dynamic Context Window (runner-emulated)\n"
            "Use this as the current project context for this turn.\n\n"
            f"{context_block}"
        )
        messages = [{"role": "system", "content": sys_base}, {"role": "system", "content": dynamic_context_system}]
        messages.extend(history[-14:])
        messages.append({"role": "user", "content": turn["user"]})

        step_log: dict[str, Any] = {
            "turn": idx,
            "mode": turn["mode"],
            "user": turn["user"],
            "context_before_chars": len(context_block),
            "prune_events_before_turn": pre_prune_events,
            "context_before_preview": context_block[:2200],
            "api_model": model,
            "api_base": base_url,
            "request_messages_exact": messages,
            "tools_offered": sorted(tool_map.keys()),
        }

        try:
            resp = post_chat_completions(base_url, api_key, model, messages, tools)
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", errors="replace")[:4000]
            step_log["http_error"] = {"code": e.code, "body": body}
            transcript.append(step_log)
            break
        except Exception as e:  # noqa: BLE001
            step_log["request_exception"] = repr(e)
            transcript.append(step_log)
            break

        content, tool_calls, finish_reason = parse_response(resp)
        step_log["finish_reason"] = finish_reason
        step_log["assistant_content"] = content
        step_log["tool_calls"] = tool_calls

        validations: list[dict[str, Any]] = []
        added_memory: list[dict[str, Any]] = []
        for tc in tool_calls:
            v = validate_tool_call(tool_map, tc["name"], tc["arguments_json"])
            validations.append({"tool": tc["name"], "arguments_json": tc["arguments_json"], "validation": v})
            if v["valid"]:
                tm = ToolMemory(
                    tool=tc["name"],
                    result=f"placeholder successful result for {tc['name']}",
                    source_turn=idx,
                    added_reason="assistant_emitted_tool_call",
                )
                state.tool_memories.append(tm)
                added_memory.append({"tool": tm.tool, "source_turn": tm.source_turn, "reason": tm.added_reason})
        step_log["tool_call_validations"] = validations
        step_log["context_additions_after_turn"] = {"tool_memories_added": added_memory}

        post_prune_events = state.prune_to_budget(eng)
        step_log["prune_events_after_turn"] = post_prune_events
        step_log["context_after_chars"] = len(state.render(eng))
        step_log["context_after_preview"] = state.render(eng)[:2200]
        transcript.append(step_log)

        # Runner-like history accumulation.
        history.append({"role": "user", "content": turn["user"]})
        history.append(
            {
                "role": "assistant",
                "content": content or "I will use tools to proceed.",
            }
        )
        time.sleep(max(0.0, args.sleep_ms / 1000.0))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    meta = {
        "timestamp_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "scenario_file": str(args.scenario),
        "api_model": model,
        "turns_requested": len(scenario),
        "turns_completed": len(transcript),
    }
    with args.out.open("w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps({"meta": meta}, ensure_ascii=True) + "\n")
        for row in transcript:
            f.write(json.dumps(row, ensure_ascii=True) + "\n")

    print(f"Wrote: {args.out}")
    print(f"Turns completed: {len(transcript)} / {len(scenario)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
