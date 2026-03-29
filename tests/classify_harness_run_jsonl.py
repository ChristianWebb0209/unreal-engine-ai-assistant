#!/usr/bin/env python3
"""
Classify harness run.jsonl outcomes for long-running headed batches.

Buckets:
  rate_limit, http_timeout, invalid_request, harness_policy, transport_other, other
  tool_finish_false — tool_finish with success:false (also in by_category)
  run_finished_ok — run_finished with success:true

Usage:
  python tests/classify_harness_run_jsonl.py --batch-root tests/long-running-tests/runs/run_20260328-120000_123
  python tests/classify_harness_run_jsonl.py --from-summary tests/long-running-tests/last-suite-summary.json
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


def classify_run_finished_error(msg: str) -> str:
    m = msg or ""
    low = m.lower()
    if "429" in m or "rate limit" in low or "tpm" in low or "rate_limit" in low:
        return "rate_limit"
    # Harness automation sync cancel — message contains "timed out" but is not HTTP transport
    if "harness run timed out" in low or "forced terminal" in low:
        return "harness_policy"
    if "timedout" in low or "timed out" in low or "timeout" in low:
        return "http_timeout"
    if "400" in m and ("http" in low or "json" in low):
        return "invalid_request"
    if "invalid json" in low or "parse the json body" in low:
        return "invalid_request"
    if "action-intent" in low or "max tool" in low or "llm rounds exceeded" in low:
        return "harness_policy"
    if m.startswith("HTTP ") and re.match(r"HTTP \d+", m):
        code = m.split()[1].rstrip(":")
        if code.isdigit() and int(code) not in (400, 429):
            return "transport_other"
    return "other"


def scan_run_jsonl(path: Path) -> dict[str, Any]:
    stats = {
        "run_finished_false": 0,
        "run_finished_true": 0,
        "tool_finish_false": 0,
        "by_category": defaultdict(int),
    }
    if not path.is_file():
        return {"error": f"missing {path}", **stats}

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            o = json.loads(line)
        except json.JSONDecodeError:
            continue
        t = o.get("type")
        if t == "run_finished":
            if o.get("success"):
                stats["run_finished_true"] += 1
                stats["by_category"]["run_finished_ok"] += 1
            else:
                stats["run_finished_false"] += 1
                cat = classify_run_finished_error(o.get("error_message") or "")
                stats["by_category"][cat] += 1
        elif t == "tool_finish" and o.get("success") is False:
            stats["tool_finish_false"] += 1
            stats["by_category"]["tool_finish_false"] += 1

    stats["by_category"] = dict(stats["by_category"])
    return stats


def find_run_jsonls_under(root: Path) -> list[Path]:
    """All run.jsonl under turns/step_* for a suite run folder or batch folder."""
    out: list[Path] = []
    for p in root.rglob("run.jsonl"):
        parts = p.parts
        if "turns" in parts and p.name == "run.jsonl":
            out.append(p)
    return sorted(out)


def rel_key(jp: Path, roots: list[Path]) -> str:
    for r in roots:
        try:
            return str(jp.relative_to(r))
        except ValueError:
            continue
    return str(jp)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--batch-root", type=Path, help="Timestamped batch folder (new layout) or any folder tree")
    ap.add_argument("--from-summary", type=Path, help="last-suite-summary.json")
    args = ap.parse_args()

    jsonls: list[Path] = []
    out_json_path: Path | None = None
    report_roots: list[Path] = []
    batch_label = ""

    if args.from_summary:
        sp = args.from_summary.resolve()
        data = json.loads(sp.read_text(encoding="utf-8-sig"))
        bo = data.get("batch_output_folder")
        suite_roots = [Path(r["run_root"]) for r in (data.get("runs") or []) if r.get("run_root")]

        if bo and Path(bo).is_dir():
            br = Path(bo).resolve()
            report_roots = [br]
            batch_label = str(br)
            jsonls = find_run_jsonls_under(br)
            if not jsonls:
                jsonls = sorted(br.rglob("run.jsonl"))
            out_json_path = br / "harness-classification.json"
        elif suite_roots:
            seen: set[str] = set()
            report_roots = [r.resolve() for r in suite_roots]
            batch_label = str(sp.parent)
            for rr in suite_roots:
                for p in find_run_jsonls_under(rr.resolve()):
                    k = str(p.resolve())
                    if k not in seen:
                        seen.add(k)
                        jsonls.append(Path(k))
            jsonls.sort()
            out_json_path = sp.parent / "harness-classification.json"
        else:
            print("Summary missing batch_output_folder and runs[].run_root", file=sys.stderr)
            return 1

    elif args.batch_root:
        br = args.batch_root.resolve()
        if not br.is_dir():
            print(f"Not a directory: {br}", file=sys.stderr)
            return 1
        report_roots = [br]
        batch_label = str(br)
        jsonls = find_run_jsonls_under(br)
        if not jsonls:
            jsonls = sorted(br.rglob("run.jsonl"))
        out_json_path = br / "harness-classification.json"
    else:
        print("Need --batch-root or --from-summary", file=sys.stderr)
        return 1

    per_file: list[dict[str, Any]] = []
    totals: dict[str, Any] = {
        "run_finished_false": 0,
        "run_finished_true": 0,
        "tool_finish_false": 0,
        "by_category": defaultdict(int),
    }

    for jp in jsonls:
        s = scan_run_jsonl(jp)
        if "error" in s:
            continue
        per_file.append({"run_jsonl": rel_key(jp, report_roots), **s})
        totals["run_finished_false"] += s["run_finished_false"]
        totals["run_finished_true"] += s["run_finished_true"]
        totals["tool_finish_false"] += s["tool_finish_false"]
        for k, v in s["by_category"].items():
            totals["by_category"][k] += v

    totals["by_category"] = dict(totals["by_category"])
    report = {
        "batch": batch_label,
        "run_jsonl_files": len(jsonls),
        "totals": totals,
        "per_file": per_file,
    }

    text = json.dumps(report, indent=2) + "\n"
    assert out_json_path is not None
    out_json_path.parent.mkdir(parents=True, exist_ok=True)
    out_json_path.write_text(text, encoding="utf-8")
    print(f"Wrote {out_json_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
