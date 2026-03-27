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
from collections import Counter
import difflib
import json
import re
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


def run_started_id(rows: list[dict[str, Any]]) -> Optional[str]:
    for r in rows:
        if r.get("type") == "run_started":
            rid = str(r.get("run_id", "") or "")
            if rid:
                return rid
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


def load_workflow_step_expectations(workflow_dir: Path) -> dict[str, list[str]]:
    wf = workflow_dir / "workflow.json"
    if not wf.is_file():
        return {}
    try:
        doc = json.loads(wf.read_text(encoding="utf-8-sig"))
    except (json.JSONDecodeError, OSError):
        return {}
    steps = doc.get("steps")
    if not isinstance(steps, list):
        return {}
    out: dict[str, list[str]] = {}
    for s in steps:
        if not isinstance(s, dict):
            continue
        sid = str(s.get("id", "") or "")
        if not sid:
            continue
        raw = s.get("expected_drop_reasons", [])
        if isinstance(raw, list):
            out[sid] = [str(x) for x in raw if isinstance(x, (str, int, float))]
    return out


def decision_logs_near(step_dir: Path) -> list[Path]:
    logs_dir = step_dir / "context_decision_logs"
    if not logs_dir.is_dir():
        return []
    return sorted(logs_dir.glob("*.jsonl"))


def summarize_decision_logs(log_paths: list[Path]) -> dict[str, Any]:
    by_reason: dict[str, dict[str, Any]] = {}
    all_kept_by_type: Counter[str] = Counter()
    all_dropped_by_type: Counter[str] = Counter()
    all_drop_reasons: Counter[str] = Counter()
    all_kept_total = 0
    all_dropped_total = 0
    for p in log_paths:
        try:
            rows = load_jsonl(p)
        except (json.JSONDecodeError, OSError):
            continue
        for r in rows:
            if r.get("event") != "candidate":
                continue
            decision = str(r.get("decision", "") or "")
            ctype = str(r.get("type", "") or "unknown")
            reason_key = str(r.get("invocationReason", "") or "unknown")
            slot = by_reason.setdefault(
                reason_key,
                {
                    "kept_total": 0,
                    "dropped_total": 0,
                    "kept_by_type": Counter(),
                    "dropped_by_type": Counter(),
                    "drop_reasons": Counter(),
                },
            )
            if decision == "kept":
                all_kept_total += 1
                all_kept_by_type[ctype] += 1
                slot["kept_total"] += 1
                slot["kept_by_type"][ctype] += 1
            elif decision == "dropped":
                all_dropped_total += 1
                all_dropped_by_type[ctype] += 1
                slot["dropped_total"] += 1
                slot["dropped_by_type"][ctype] += 1
                reason = str(r.get("reason", "") or "unknown")
                all_drop_reasons[reason] += 1
                slot["drop_reasons"][reason] += 1
    request = by_reason.get(
        "request_build",
        {
            "kept_total": 0,
            "dropped_total": 0,
            "kept_by_type": Counter(),
            "dropped_by_type": Counter(),
            "drop_reasons": Counter(),
        },
    )
    warnings: list[str] = []
    if "request_build" not in by_reason and len(log_paths) > 0:
        warnings.append("No request_build decision logs found; headline ranking metrics may reflect dump-only context builds.")
    by_reason_json: dict[str, Any] = {}
    for key, slot in by_reason.items():
        by_reason_json[key] = {
            "kept_total": slot["kept_total"],
            "dropped_total": slot["dropped_total"],
            "kept_by_type": dict(slot["kept_by_type"]),
            "dropped_by_type": dict(slot["dropped_by_type"]),
            "top_drop_reasons": [
                {"reason": reason, "count": count}
                for reason, count in slot["drop_reasons"].most_common(8)
            ],
        }
    return {
        "log_file_count": len(log_paths),
        "headlined_reason": "request_build",
        "kept_total": request["kept_total"],
        "dropped_total": request["dropped_total"],
        "kept_by_type": dict(request["kept_by_type"]),
        "dropped_by_type": dict(request["dropped_by_type"]),
        "top_drop_reasons": [
            {"reason": reason, "count": count}
            for reason, count in request["drop_reasons"].most_common(8)
        ],
        "all_invocations": {
            "kept_total": all_kept_total,
            "dropped_total": all_dropped_total,
            "kept_by_type": dict(all_kept_by_type),
            "dropped_by_type": dict(all_dropped_by_type),
            "top_drop_reasons": [
                {"reason": reason, "count": count}
                for reason, count in all_drop_reasons.most_common(8)
            ],
        },
        "by_invocation_reason": by_reason_json,
        "warnings": warnings,
    }


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
        thread_id = tid.read_text(encoding="utf-8").lstrip("\ufeff").strip()
    expected_drop_by_step = load_workflow_step_expectations(wf)

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
    seen_run_ids: dict[str, list[str]] = {}

    for step_name, step_path in steps:
        jl = step_path / "run.jsonl"
        rows = load_jsonl(jl)
        tools = tools_from_jsonl(rows)
        ok = run_finished_success(rows)
        run_id = run_started_id(rows)
        if run_id:
            seen_run_ids.setdefault(run_id, []).append(step_name)
        decision_logs = decision_logs_near(step_path)
        ranking = summarize_decision_logs(decision_logs)
        m = re.match(r"^step_\d+_(.+)$", step_name)
        logical_step_id = m.group(1) if m else step_name
        expected_drop_reasons = expected_drop_by_step.get(logical_step_id, [])
        # Expectations should be validated against the "real" context build for the model turn.
        # Dump-only invocations (harness dumps) can have radically different candidate sets.
        all_drop_reason_set = {str(x["reason"]) for x in ranking["top_drop_reasons"]}
        expected_drop_hit_count = sum(1 for r in expected_drop_reasons if r in all_drop_reason_set)
        expected_drop_total = len(expected_drop_reasons)
        # Treat expected_drop_reasons as a "should see at least one of these" hint, not a hard requirement
        # to see every reason in a single run (which is brittle as packing behavior evolves).
        expected_drop_coverage_ok = True if expected_drop_total == 0 else expected_drop_hit_count >= 1
        ctx_files = sorted(step_path.glob("context_window*.txt"))
        finished = step_path / "context_window_run_finished.txt"
        finished_lines = -1
        if finished.is_file():
            finished_lines = sum(1 for _ in finished.open(encoding="utf-8", errors="replace"))

        entry: dict[str, Any] = {
            "step_dir": step_name,
            "run_jsonl": str(jl),
            "run_id": run_id,
            "run_finished_success": ok,
            "row_count": len(rows),
            "tools_called": tools,
            "ranking_metrics": ranking,
            "expected_drop_reasons": expected_drop_reasons,
            "expected_drop_reasons_hit_count": expected_drop_hit_count,
            "expected_drop_reasons_total": expected_drop_total,
            "expected_drop_reasons_ok": expected_drop_coverage_ok,
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
        md.append(f"- **run_id**: `{run_id}`")
        md.append(f"- **Tools (order)**: {', '.join(tools) if tools else '(none)'}")
        md.append(
            f"- **Ranking metrics (request_build)**: decision_logs={ranking['log_file_count']} kept={ranking['kept_total']} dropped={ranking['dropped_total']}"
        )
        md.append(
            f"- **Ranking metrics (all invocations)**: kept={ranking['all_invocations']['kept_total']} dropped={ranking['all_invocations']['dropped_total']}"
        )
        if ranking["top_drop_reasons"]:
            top_reasons = ", ".join(
                f"{x['reason']} ({x['count']})" for x in ranking["top_drop_reasons"][:3]
            )
            md.append(f"- **Top drop reasons**: {top_reasons}")
        if expected_drop_total > 0:
            md.append(
                f"- **expected_drop_reasons**: {expected_drop_hit_count}/{expected_drop_total} "
                f"({'pass' if expected_drop_coverage_ok else 'fail'})"
            )
        if ranking["warnings"]:
            for w in ranking["warnings"]:
                md.append(f"- **Ranking warning**: {w}")
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

    duplicate_run_ids = {rid: step_names for rid, step_names in seen_run_ids.items() if rid and len(step_names) > 1}
    if duplicate_run_ids:
        md.append("## Harness integrity warnings")
        md.append("")
        for rid, step_names in sorted(duplicate_run_ids.items()):
            md.append(f"- Duplicate `run_id` `{rid}` appears in: {', '.join(step_names)}")
        md.append("")
        for entry in bundle_steps:
            rid = entry.get("run_id")
            if rid in duplicate_run_ids:
                entry["artifact_integrity_warning"] = "duplicate_run_id_across_steps"

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
