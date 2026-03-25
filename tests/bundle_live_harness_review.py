#!/usr/bin/env python3
"""
Aggregate live harness outputs (run.jsonl + context_window_*.txt paths) into review.md and review.json
for qualitative (human or external LLM) review. Does not judge quality.

Usage:
  python tests/bundle_live_harness_review.py tests/out/live_runs/tool_goals
  python tests/bundle_live_harness_review.py Saved/UnrealAiEditor/HarnessRuns/20250324_120000
  python tests/bundle_live_harness_review.py path/to/run.jsonl
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Optional


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    text = path.read_text(encoding="utf-8-sig")
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        rows.append(json.loads(line))
    return rows


def tools_from_rows(rows: list[dict[str, Any]]) -> list[str]:
    ordered: list[str] = []
    seen: set[str] = set()
    for r in rows:
        if r.get("type") != "tool_start":
            continue
        t = str(r.get("tool", "") or "")
        if t and t not in seen:
            seen.add(t)
            ordered.append(t)
    return ordered


def run_finished_success(rows: list[dict[str, Any]]) -> Optional[bool]:
    for r in reversed(rows):
        if r.get("type") == "run_finished":
            return bool(r.get("success", False))
    return None


def discover_runs(root: Path) -> list[tuple[str, Path]]:
    if root.is_file():
        if root.name != "run.jsonl":
            print(f"Expected run.jsonl, got: {root}", file=sys.stderr)
            sys.exit(1)
        return [(root.parent.name, root)]
    out: list[tuple[str, Path]] = []
    if not root.is_dir():
        print(f"Not found: {root}", file=sys.stderr)
        sys.exit(1)
    for d in sorted(root.iterdir(), key=lambda p: p.name):
        if not d.is_dir():
            continue
        jl = d / "run.jsonl"
        if jl.is_file():
            out.append((d.name, jl))
    if not out and (root / "run.jsonl").is_file():
        return [(root.name, root / "run.jsonl")]
    return out


def context_dumps_near(jsonl: Path) -> list[tuple[str, int]]:
    parent = jsonl.parent
    found: list[tuple[str, int]] = []
    for p in sorted(parent.glob("context_window*.txt")):
        try:
            n = sum(1 for _ in p.open(encoding="utf-8", errors="replace"))
        except OSError:
            n = -1
        found.append((str(p), n))
    return found


def main() -> int:
    p = argparse.ArgumentParser(description="Bundle live harness JSONL + context paths into review.md / review.json")
    p.add_argument(
        "input",
        type=Path,
        help="Suite output directory (scenario folders), a single harness run folder, or run.jsonl",
    )
    p.add_argument(
        "-o",
        "--output-dir",
        type=Path,
        default=None,
        help="Write review.md and review.json here (default: same as input if directory, else parent)",
    )
    args = p.parse_args()
    root = args.input.resolve()
    runs = discover_runs(root)
    if not runs:
        print(f"No run.jsonl found under {root}", file=sys.stderr)
        return 1

    out_dir = args.output_dir
    if out_dir is None:
        out_dir = root if root.is_dir() else root.parent
    out_dir = out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    bundle: list[dict[str, Any]] = []
    md_lines: list[str] = [
        "# Live harness review bundle",
        "",
        "Qualitative notes: fill in below per scenario (human or external LLM).",
        "",
    ]

    for scenario_id, jsonl in runs:
        try:
            rows = load_jsonl(jsonl)
        except (json.JSONDecodeError, OSError) as e:
            print(f"Skip {scenario_id}: {e}", file=sys.stderr)
            continue
        tools = tools_from_rows(rows)
        ok = run_finished_success(rows)
        ctx = context_dumps_near(jsonl)
        entry = {
            "scenario_id": scenario_id,
            "run_jsonl": str(jsonl),
            "run_finished_success": ok,
            "row_count": len(rows),
            "tools_called": tools,
            "context_window_files": [{"path": c[0], "line_count": c[1]} for c in ctx],
        }
        bundle.append(entry)

        md_lines.append(f"## {scenario_id}")
        md_lines.append("")
        md_lines.append(f"- **run.jsonl**: `{jsonl}` ({len(rows)} lines)")
        md_lines.append(f"- **run_finished success**: {ok}")
        md_lines.append(f"- **Tools (tool_start order)**: {', '.join(tools) if tools else '(none)'}")
        if ctx:
            for cpath, ln in ctx:
                md_lines.append(f"- **Context dump**: `{cpath}` ({ln} lines)")
        else:
            md_lines.append("- **Context dumps**: (none — set `UNREAL_AI_HARNESS_DUMP_CONTEXT=1` or `dumpcontext` on RunAgentTurn)")
        md_lines.append("")
        md_lines.append("### Qualitative checklist")
        md_lines.append("")
        md_lines.append("- [ ] Meets expected outcome (see docs/tool-goals.md for `source_task` in manifest)")
        md_lines.append("- [ ] Notes:")
        md_lines.append("")
        md_lines.append("---")
        md_lines.append("")

    review_json = out_dir / "review.json"
    review_md = out_dir / "review.md"
    review_json.write_text(json.dumps({"scenarios": bundle}, indent=2), encoding="utf-8")
    review_md.write_text("\n".join(md_lines).rstrip() + "\n", encoding="utf-8")
    print(f"Wrote {review_json}")
    print(f"Wrote {review_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
