#!/usr/bin/env python3
"""
Summarize tests/out/last-matrix.json (tool catalog matrix run) for humans and LLM agents.

Python 3.9+. Stdlib only.

Exit codes:
  0 — no contract violations and no rows with tier contract_fail
  1 — violations, contract_fail rows, parse errors, or --strict rows with bOk false
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parent.parent


def load_matrix(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main() -> int:
    p = argparse.ArgumentParser(description="Summarize Unreal AI tool_matrix_last.json")
    p.add_argument(
        "--matrix",
        type=Path,
        help="Path to matrix JSON (default: tests/out/last-matrix.json)",
    )
    p.add_argument(
        "--strict",
        action="store_true",
        help="Treat any result row with bOk=false as failure (default: only contract violations / contract_fail tier).",
    )
    args = p.parse_args()
    root = repo_root_from_script()
    matrix_path = args.matrix or (root / "tests" / "out" / "last-matrix.json")

    if not matrix_path.is_file():
        print(f"Matrix file not found: {matrix_path}", file=sys.stderr)
        return 2

    try:
        data = load_matrix(matrix_path)
    except (OSError, json.JSONDecodeError) as e:
        print(f"Failed to read JSON: {e}", file=sys.stderr)
        return 2

    summary = data.get("summary") if isinstance(data.get("summary"), dict) else {}
    violations = data.get("contract_violations")
    if not isinstance(violations, list):
        violations = []
    results = data.get("results")
    if not isinstance(results, list):
        results = []

    cv = int(summary.get("contract_violations") or 0)
    invoked = int(summary.get("invoked") or 0)
    tools_total = int(summary.get("tools_total") or 0)

    print("=== tool matrix summary ===")
    print(f"file:          {matrix_path}")
    print(f"tools_total:   {tools_total}")
    print(f"invoked:       {invoked}")
    print(f"contract_violations (count): {cv}")
    print()

    exit_fail = 0
    if cv > 0:
        exit_fail = 1
        print("--- contract_violations ---")
        for v in violations:
            if not isinstance(v, dict):
                continue
            tid = v.get("tool_id", "")
            reason = v.get("reason", "")
            print(f"  {tid}: {reason}")
        print()

    contract_fail_rows: list[str] = []
    bok_false_rows: list[str] = []
    for r in results:
        if not isinstance(r, dict):
            continue
        if r.get("skipped"):
            continue
        tid = str(r.get("tool_id") or "")
        tier = str(r.get("tier") or "")
        if tier == "contract_fail":
            contract_fail_rows.append(tid)
            err = r.get("error_message") or r.get("response_preview") or ""
            print(f"[contract_fail] {tid}: {str(err)[:300]}")
        b_ok = r.get("bOk")
        if args.strict and b_ok is False:
            bok_false_rows.append(tid)

    if contract_fail_rows:
        exit_fail = 1
        print()
        print(f"Rows with tier=contract_fail: {len(contract_fail_rows)}")

    if args.strict and bok_false_rows:
        exit_fail = 1
        print()
        print(f"--strict: bOk=false tools: {', '.join(sorted(set(bok_false_rows)))}")

    if exit_fail == 0:
        print("OK: no contract violations" + (" (strict: no bOk=false)" if args.strict else ""))
    else:
        print()
        print("FAIL: fix dispatch/catalog contract; see docs/tooling/AGENT_HARNESS_HANDOFF.md")

    return exit_fail


if __name__ == "__main__":
    raise SystemExit(main())
