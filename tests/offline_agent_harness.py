#!/usr/bin/env python3
"""
Offline harness for Unreal AI agent prompt/context/tool-call iteration.

Goals:
- No Unreal process required.
- Build context window + system prompt from repository files.
- Feed vague user prompts and emulate model tool-call selection.
- Emit deterministic artifacts for regression checks.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from tool_catalog_routing_check import (
    build_system_developer_content,
    build_tool_ids_for_mode,
    default_catalog_path,
    parse_mode,
    repo_root_from_script,
)


def _tokenize(text: str) -> set[str]:
    words = re.findall(r"[A-Za-z0-9_]+", text.lower())
    return {w for w in words if len(w) > 2}


TOOL_HINTS: dict[str, set[str]] = {
    "editor_get_selection": {"selected", "selection", "picked", "level", "actors", "currently"},
    "asset_registry_query": {"find", "locate", "list", "search", "asset", "blueprint", "material", "shader"},
    "blueprint_export_ir": {"blueprint", "graph", "inspect", "event", "node"},
    "material_get_parameters": {"material", "shader", "glossy", "roughness", "specular", "floor"},
    "material_get_usage_summary": {"material", "shader", "glossy", "roughness", "specular", "floor"},
}


@dataclass
class ContextState:
    attachments: list[str] = field(default_factory=list)
    tool_results: list[dict[str, str]] = field(default_factory=list)
    selected_assets: list[str] = field(default_factory=list)
    content_browser_path: str = "/Game"

    def build_context_block(self, engine_label: str) -> str:
        lines: list[str] = [f"### (({engine_label}))", ""]
        if self.selected_assets:
            lines.append("content_browser_selected_assets:")
            for a in self.selected_assets[:8]:
                lines.append(f"- {a}")
            lines.append("")
        if self.attachments:
            lines.append("attachments:")
            for a in self.attachments[:10]:
                lines.append(f"- {a}")
            lines.append("")
        if self.tool_results:
            lines.append("tool_results:")
            for tr in self.tool_results[-8:]:
                tool = tr.get("tool", "")
                result = (tr.get("result", "") or "")[:220].replace("\n", " ")
                lines.append(f"- {tool}: {result}")
            lines.append("")
        lines.append(f"content_browser_path: {self.content_browser_path}")
        return "\n".join(lines).strip() + "\n"


def _engine_label(root: Path) -> str:
    for uproj in root.glob("*.uproject"):
        try:
            data = json.loads(uproj.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        eng = data.get("EngineAssociation")
        if isinstance(eng, str) and eng:
            return f"Unreal Engine {eng}"
    return "Unreal Engine 5.7"


def load_scenarios(path: Path) -> list[dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    cases = data.get("cases")
    if not isinstance(cases, list):
        raise ValueError(f"{path} missing cases[]")
    out: list[dict[str, Any]] = []
    for c in cases:
        if not isinstance(c, dict):
            continue
        prompt = str(c.get("prompt") or "").strip()
        if not prompt:
            continue
        mode = parse_mode(str(c.get("mode") or "agent"))
        expected = c.get("expected_tool_calls") or []
        if not isinstance(expected, list):
            expected = []
        out.append(
            {
                "id": str(c.get("id") or f"case_{len(out)+1}"),
                "mode": mode,
                "prompt": prompt,
                "expected_tool_calls": [str(x) for x in expected if x],
                "attachments": [str(x) for x in (c.get("attachments") or []) if x],
                "selected_assets": [str(x) for x in (c.get("selected_assets") or []) if x],
            }
        )
    return out


def _tool_entries_for_mode(catalog: dict[str, Any], mode: str) -> list[dict[str, Any]]:
    allowed = build_tool_ids_for_mode(catalog, mode, supports_native_tools=True)
    tools = catalog.get("tools")
    if not isinstance(tools, list):
        return []
    out: list[dict[str, Any]] = []
    for t in tools:
        if not isinstance(t, dict):
            continue
        tid = t.get("tool_id")
        if not isinstance(tid, str) or tid not in allowed:
            continue
        out.append(t)
    return out


def rank_tools_for_prompt(catalog: dict[str, Any], mode: str, prompt: str, context: ContextState) -> list[tuple[str, float]]:
    p_tokens = _tokenize(prompt)
    ctx_tokens = _tokenize(" ".join(context.attachments + context.selected_assets + [context.content_browser_path]))
    ranked: list[tuple[str, float]] = []
    for t in _tool_entries_for_mode(catalog, mode):
        tid = str(t.get("tool_id") or "")
        summary = str(t.get("summary") or "")
        search = f"{tid} {summary} {json.dumps(t.get('aliases', []), ensure_ascii=True)}"
        t_tokens = _tokenize(search)
        if not t_tokens:
            continue
        overlap_prompt = len(p_tokens & t_tokens)
        overlap_ctx = len(ctx_tokens & t_tokens)
        score = overlap_prompt * 3.0 + overlap_ctx * 1.2
        if tid.lower() in prompt.lower():
            score += 12.0
        if "blueprint" in prompt.lower() and "blueprint" in search.lower():
            score += 2.5
        if "material" in prompt.lower() and "material" in search.lower():
            score += 2.5
        hints = TOOL_HINTS.get(tid)
        if hints:
            score += 1.8 * len(p_tokens & hints)
        if tid == "source_search_symbol" and not any(
            x in prompt.lower() for x in ["symbol", "source", "cpp", "c++", "header", "function", "class", "code"]
        ):
            score -= 6.0
        if tid == "asset_registry_query" and any(x in prompt.lower() for x in ["find", "locate", "list", "search"]):
            score += 2.2
        # Penalize direct mutators when the prompt asks to inspect/check first.
        if any(x in prompt.lower() for x in ["inspect", "check", "before changing", "read", "look at"]):
            if any(x in tid for x in ["set_", "apply", "compile", "delete", "rename"]):
                score -= 3.0
        if score > 0:
            ranked.append((tid, score))
    ranked.sort(key=lambda x: x[1], reverse=True)
    return ranked


def emulate_agent_tool_calls(catalog: dict[str, Any], mode: str, prompt: str, context: ContextState, max_calls: int) -> list[str]:
    ranked = rank_tools_for_prompt(catalog, mode, prompt, context)
    if not ranked:
        return []
    if len(ranked) == 1:
        return [ranked[0][0]]
    # Keep multiple calls only when confidence is close.
    best = ranked[0][1]
    keep = [tid for tid, score in ranked if score >= (best * 0.72)]
    keep = keep[: max(1, max_calls)]

    allowed = build_tool_ids_for_mode(catalog, mode, supports_native_tools=True)
    p = prompt.lower()
    wants_discovery = any(x in p for x in ["find", "locate", "search", "list"])
    if wants_discovery and "asset_registry_query" in allowed and "asset_registry_query" not in keep:
        if any(t in keep for t in ["blueprint_export_ir", "material_get_usage_summary"]):
            keep.insert(0, "asset_registry_query")
    return keep[: max(1, max_calls)]


def run_cases(
    root: Path,
    catalog: dict[str, Any],
    scenarios: list[dict[str, Any]],
    *,
    thread_id: str,
    max_calls: int,
) -> tuple[list[dict[str, Any]], int]:
    state = ContextState()
    engine = _engine_label(root)
    rows: list[dict[str, Any]] = []
    failures = 0

    for i, c in enumerate(scenarios, start=1):
        state.attachments = list(c["attachments"])
        state.selected_assets = list(c["selected_assets"])
        context_block = state.build_context_block(engine)
        system = build_system_developer_content(
            root,
            c["mode"],
            c["prompt"],
            thread_id,
            llm_round=i,
            max_llm_rounds=len(scenarios),
        )
        selected = emulate_agent_tool_calls(catalog, c["mode"], c["prompt"], state, max_calls=max_calls)
        expected = c["expected_tool_calls"]
        missing = [e for e in expected if e not in selected]
        ok = len(missing) == 0
        if not ok:
            failures += 1

        for tid in selected:
            state.tool_results.append(
                {"tool": tid, "result": f"offline harness placeholder result for {tid}"}
            )

        rows.append(
            {
                "id": c["id"],
                "mode": c["mode"],
                "prompt": c["prompt"],
                "expected_tool_calls": expected,
                "tool_calls": selected,
                "missing_expected": missing,
                "ok": ok,
                "context_block_preview": context_block[:700],
                "system_prompt_preview": system[:1400],
            }
        )
    return rows, failures


def write_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=True) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Offline context+prompt+tool-call harness (no Unreal runtime).")
    parser.add_argument(
        "--scenarios",
        type=Path,
        default=Path("tests/offline_harness_scenarios.json"),
        help="Scenario JSON file with vague prompts and expected tools.",
    )
    parser.add_argument(
        "--catalog",
        type=Path,
        default=None,
        help="Override UnrealAiToolCatalog.json path.",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("tests/out/offline_harness/latest.jsonl"),
        help="Output JSONL report path.",
    )
    parser.add_argument(
        "--thread-id",
        default="offline_harness_thread",
        help="Synthetic thread id for prompt token replacement.",
    )
    parser.add_argument("--max-calls", type=int, default=2, help="Max tool calls emitted per case.")
    args = parser.parse_args()

    root = repo_root_from_script()
    catalog_path = args.catalog or default_catalog_path(root)
    if not catalog_path.is_file():
        print(f"Catalog not found: {catalog_path}", file=sys.stderr)
        return 2
    if not args.scenarios.is_file():
        print(f"Scenario file not found: {args.scenarios}", file=sys.stderr)
        return 2

    catalog = json.loads(catalog_path.read_text(encoding="utf-8-sig"))
    scenarios = load_scenarios(args.scenarios)
    if not scenarios:
        print("No runnable scenarios found.", file=sys.stderr)
        return 2

    rows, failures = run_cases(
        root,
        catalog,
        scenarios,
        thread_id=args.thread_id,
        max_calls=max(1, args.max_calls),
    )
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    write_jsonl(args.out, [{"timestamp_utc": stamp, "scenario_count": len(scenarios), "failures": failures}] + rows)

    print(f"Wrote: {args.out}")
    print(f"Scenarios: {len(scenarios)}  Failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
