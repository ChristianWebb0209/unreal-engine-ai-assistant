#!/usr/bin/env python3
"""
Validate a harness run.jsonl (JSON lines) from UnrealAi.RunAgentTurn / FAgentRunFileSink.

Usage:
  python tests/assert_harness_run.py tests/out/sample-run.jsonl
  python tests/assert_harness_run.py tests/out/sample-run.jsonl --expect-tool editor_get_selection

Exit 0 if validation passes; 1 if missing file / parse error; 2 if expectation fails.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    text = path.read_text(encoding="utf-8-sig")
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        rows.append(json.loads(line))
    return rows


def main() -> int:
    p = argparse.ArgumentParser(description="Assert harness run.jsonl structure and optional tool names")
    p.add_argument("jsonl", type=Path, help="Path to run.jsonl")
    p.add_argument(
        "--expect-tool",
        action="append",
        default=[],
        help="Tool id that must appear in a tool_start or tool_finish row (repeatable)",
    )
    p.add_argument(
        "--require-success",
        action="store_true",
        help="Last run_finished row must have success true",
    )
    args = p.parse_args()
    path: Path = args.jsonl
    if not path.is_file():
        print(f"Not found: {path}", file=sys.stderr)
        return 1
    try:
        rows = load_jsonl(path)
    except json.JSONDecodeError as e:
        print(f"Invalid JSONL: {e}", file=sys.stderr)
        return 1
    if not rows:
        print("Empty JSONL", file=sys.stderr)
        return 1
    types = [r.get("type") for r in rows]
    if types[0] != "run_started":
        print("Expected first row type run_started", file=sys.stderr)
        return 2
    if types[-1] != "run_finished":
        print("Expected last row type run_finished", file=sys.stderr)
        return 2
    if args.require_success and not rows[-1].get("success", False):
        print("run_finished success was false", file=sys.stderr)
        return 2
    tools_seen: set[str] = set()
    for r in rows:
        t = r.get("type")
        if t == "tool_start":
            tools_seen.add(str(r.get("tool", "")))
        elif t == "tool_finish":
            tools_seen.add(str(r.get("tool", "")))
    for need in args.expect_tool:
        if need not in tools_seen:
            print(f"Expected tool not seen: {need} (have {sorted(tools_seen)})", file=sys.stderr)
            return 2
    print(f"OK: {len(rows)} lines, run_finished success={rows[-1].get('success')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
