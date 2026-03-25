#!/usr/bin/env python3
"""
Bundle context workflow harness outputs: per-step run.jsonl summary and optional unified diff
between successive context_window_run_finished.txt files for qualitative review.

Usage:
  python tests/bundle_context_workflow_review.py tests/out/context_runs/context_pilots/conv_memory_smoke
  python tests/bundle_context_workflow_review.py path/to/step_folder --no-diff
"""
from __future__ import annotations

import argparse
import difflib
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


def tools_from_jsonl(rows: list[dict[str, Any]]) -> list[str]:
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


def discover_steps(workflow_dir: Path) -> list[tuple[str, Path]]:
    """Return sorted list of (step_dir_name, path) for step_* subdirs containing run.jsonl."""
    if not workflow_dir.is_dir():
        print(f"Not a directory: {workflow_dir}", file=sys.stderr)
        sys.exit(1)
    out: list[tuple[str, Path]] = []
    for d in sorted(workflow_dir.iterdir(), key=lambda p: p.name):
        if not d.is_dir():
            continue
        if not d.name.startswith("step_"):
            continue
        jl = d / "run.jsonl"
        if jl.is_file():
            out.append((d.name, d))
    return out


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    p = argparse.ArgumentParser(
        description="Bundle context workflow step folders: JSONL tools + optional context dump diffs"
    )
    p.add_argument(
        "workflow_dir",
        type=Path,
        help="Output folder for one workflow (contains step_* subdirs from run-headed-context-workflows.ps1)",
    )
    p.add_argument(
        "--no-diff",
        action="store_true",
        help="Skip unified diff between successive context_window_run_finished.txt",
    )
    p.add_argument(
        "-o",
        "--output-dir",
        type=Path,
        default=None,
        help="Write context_review.md/json here (default: workflow_dir)",
    )
    args = p.parse_args()
    wf = args.workflow_dir.resolve()
    steps = discover_steps(wf)
    if not steps:
        print(f"No step_* folders with run.jsonl under {wf}", file=sys.stderr)
        return 1

    out_dir = args.output_dir.resolve() if args.output_dir else wf
    out_dir.mkdir(parents=True, exist_ok=True)

    thread_id: Optional[str] = None
    tid = wf / "thread_id.txt"
    if tid.is_file():
        thread_id = tid.read_text(encoding="utf-8").strip()

    bundle_steps: list[dict[str, Any]] = []
    md: list[str] = [
        "# Context workflow review bundle",
        "",
        f"- **workflow_dir**: `{wf}`",
    ]
    if thread_id:
        md.append(f"- **thread_id**: `{thread_id}`")
    md.extend(["", "Qualitative notes: fill in what changed between steps and any concerns.", ""])

    prev_finished: Optional[str] = None
    prev_name: Optional[str] = None

    for step_name, step_path in steps:
        jl = step_path / "run.jsonl"
        rows = load_jsonl(jl)
        tools = tools_from_jsonl(rows)
        ok = run_finished_success(rows)
        ctx_files = sorted(step_path.glob("context_window*.txt"))
        finished = step_path / "context_window_run_finished.txt"
        finished_lines = -1
        if finished.is_file():
            finished_lines = sum(1 for _ in finished.open(encoding="utf-8", errors="replace"))

        entry: dict[str, Any] = {
            "step_dir": step_name,
            "run_jsonl": str(jl),
            "run_finished_success": ok,
            "row_count": len(rows),
            "tools_called": tools,
            "context_dump_files": [str(c) for c in ctx_files],
            "context_window_run_finished_lines": finished_lines,
        }

        diff_text = ""
        if (
            not args.no_diff
            and prev_finished is not None
            and prev_name is not None
            and finished.is_file()
        ):
            cur = read_text(finished)
            diff_lines = difflib.unified_diff(
                prev_finished.splitlines(keepends=True),
                cur.splitlines(keepends=True),
                fromfile=f"{prev_name}/context_window_run_finished.txt",
                tofile=f"{step_name}/context_window_run_finished.txt",
                lineterm="",
            )
            diff_text = "".join(diff_lines)
            entry["diff_from_previous_run_finished"] = diff_text

        bundle_steps.append(entry)

        md.append(f"## {step_name}")
        md.append("")
        md.append(f"- **run.jsonl**: `{jl}` ({len(rows)} lines), success={ok}")
        md.append(f"- **Tools (order)**: {', '.join(tools) if tools else '(none)'}")
        md.append(f"- **context_window_run_finished.txt**: {finished_lines} lines")
        if ctx_files:
            md.append(f"- **All context dumps**: {len(ctx_files)} file(s)")
        if diff_text:
            md.append("")
            md.append("### Diff vs previous step (run_finished)")
            md.append("")
            md.append("```diff")
            md.append(diff_text.rstrip()[:200000] or "(empty)")
            md.append("```")
        md.append("")
        md.append("### Qualitative notes")
        md.append("")
        md.append("- ")
        md.append("")
        md.append("---")
        md.append("")

        if finished.is_file():
            prev_finished = read_text(finished)
            prev_name = step_name
        else:
            prev_finished = None
            prev_name = step_name

    review_json = out_dir / "context_review.json"
    review_md = out_dir / "context_review.md"
    review_json.write_text(
        json.dumps({"workflow_dir": str(wf), "thread_id": thread_id, "steps": bundle_steps}, indent=2),
        encoding="utf-8",
    )
    review_md.write_text("\n".join(md).rstrip() + "\n", encoding="utf-8")
    print(f"Wrote {review_json}")
    print(f"Wrote {review_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
