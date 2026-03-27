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
from collections import Counter
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


def planning_decisions(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for r in rows:
        if r.get("type") != "planning_decision":
            continue
        out.append(
            {
                "mode_used": str(r.get("mode_used", "") or ""),
                "trigger_reasons": list(r.get("trigger_reasons", []) or []),
                "replan_count": int(r.get("replan_count", 0) or 0),
                "queue_steps_pending": int(r.get("queue_steps_pending", 0) or 0),
            }
        )
    return out


def run_finished_success(rows: list[dict[str, Any]]) -> Optional[bool]:
    for r in reversed(rows):
        if r.get("type") == "run_finished":
            return bool(r.get("success", False))
    return None


def load_scenario_status(jsonl: Path) -> dict[str, Any]:
    candidates = [jsonl.parent / "scenario_status.json", jsonl.parent.parent / "scenario_status.json"]
    p = None
    for c in candidates:
        if c.is_file():
            p = c
            break
    if p is None:
        return {}
    try:
        return json.loads(p.read_text(encoding="utf-8-sig"))
    except (json.JSONDecodeError, OSError):
        return {}


def infer_agent_status(rows: list[dict[str, Any]], run_finished_ok: Optional[bool]) -> str:
    if run_finished_ok is True:
        return "success"
    if run_finished_ok is False:
        for r in reversed(rows):
            if r.get("type") != "run_finished":
                continue
            msg = str(r.get("error_message", "") or "").lower()
            if "max tool/llm rounds exceeded" in msg or "max rounds" in msg:
                return "round_cap"
            if "blocked" in msg:
                return "blocked"
            return "error"
    return "unknown"


def infer_artifact_integrity(rows: list[dict[str, Any]]) -> str:
    if not rows:
        return "missing"
    has_started = any(r.get("type") == "run_started" for r in rows)
    has_finished = any(r.get("type") == "run_finished" for r in rows)
    if has_started and has_finished:
        return "ok"
    if has_started or has_finished:
        return "partial"
    return "stale"


def infer_run_metrics(rows: list[dict[str, Any]]) -> dict[str, Any]:
    call_id_to_args: dict[str, str] = {}
    fail_counts: Counter[str] = Counter()

    normalization_applied_count = 0

    low_confidence_search_count = 0
    low_confidence_prev_sig: Optional[str] = None
    low_confidence_streak = 0
    low_confidence_max_streak = 0

    def norm_args(s: str) -> str:
        s = re.sub(r"\s+", " ", s or "").strip()
        return s[:160]

    def extract_query(args_json: str) -> Optional[str]:
        if not args_json:
            return None
        # Prefer canonical key; then accept legacy-but-tolerated keys for grouping.
        for k in ("query", "search_string", "name_prefix", "filter"):
            m = re.search(rf'"{re.escape(k)}"\s*:\s*"([^"]+)"', args_json)
            if m:
                return m.group(1)
        return None

    for r in rows:
        rtype = str(r.get("type") or "")
        if rtype == "tool_start":
            call_id = str(r.get("call_id") or "")
            call_id_to_args[call_id] = str(r.get("arguments_json") or "")
            continue
        if rtype != "tool_finish":
            continue

        tool = str(r.get("tool") or "")
        call_id = str(r.get("call_id") or "")
        success = bool(r.get("success", False))
        result_preview = str(r.get("result_preview") or "")
        args_json = call_id_to_args.get(call_id, "")

        if not success:
            shape = f"{tool}|{norm_args(args_json)}"
            fail_counts[shape] += 1

        if re.search(r'"normalization_applied"\s*:\s*true', result_preview):
            normalization_applied_count += 1

        if tool in ("asset_index_fuzzy_search", "scene_fuzzy_search"):
            if re.search(r'"low_confidence"\s*:\s*true', result_preview):
                low_confidence_search_count += 1
                q = extract_query(args_json) or norm_args(args_json)
                sig = f"{tool}|{q}"
                if low_confidence_prev_sig == sig:
                    low_confidence_streak += 1
                else:
                    low_confidence_prev_sig = sig
                    low_confidence_streak = 1
                low_confidence_max_streak = max(low_confidence_max_streak, low_confidence_streak)

    top_failures = [
        {"tool_shape": shape, "count": int(count)}
        for shape, count in fail_counts.most_common(5)
    ]
    max_fail = int(top_failures[0]["count"]) if top_failures else 0

    # Flag likely non-progress loops for faster iteration.
    non_progress_detected = (max_fail >= 3) or (low_confidence_max_streak >= 3)
    non_progress_reason: list[str] = []
    if max_fail >= 3:
        non_progress_reason.append(f"repeated_tool_validation_failure|max_fail={max_fail}")
    if low_confidence_max_streak >= 3:
        non_progress_reason.append(
            f"repeated_low_confidence_search|max_streak={low_confidence_max_streak}"
        )

    return {
        "tool_failure_top": top_failures,
        "normalization_applied_count": normalization_applied_count,
        "low_confidence_search_count": low_confidence_search_count,
        "low_confidence_max_streak": low_confidence_max_streak,
        "non_progress_detected": non_progress_detected,
        "non_progress_reason": non_progress_reason,
    }


def discover_runs(root: Path) -> list[tuple[str, Optional[Path]]]:
    if root.is_file():
        if root.name != "run.jsonl":
            print(f"Expected run.jsonl, got: {root}", file=sys.stderr)
            sys.exit(1)
        return [(root.parent.name, root)]
    out: list[tuple[str, Optional[Path]]] = []
    if not root.is_dir():
        print(f"Not found: {root}", file=sys.stderr)
        sys.exit(1)
    for d in sorted(root.iterdir(), key=lambda p: p.name):
        if not d.is_dir():
            continue
        jl = d / "run.jsonl"
        if jl.is_file():
            out.append((d.name, jl))
            continue
        # New harness layout may write attempt_NN/run.jsonl per scenario.
        attempts = sorted(d.glob("attempt_*/run.jsonl"))
        if attempts:
            out.append((d.name, attempts[-1]))
            continue
        # Include status-only scenarios so infra failures without run.jsonl still appear in review.
        if (d / "scenario_status.json").is_file():
            out.append((d.name, None))
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


def decision_logs_near(jsonl: Path) -> list[Path]:
    parent = jsonl.parent
    logs_dir = parent / "context_decision_logs"
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


def load_manifest_scenarios(path: Optional[Path]) -> dict[str, dict[str, Any]]:
    if path is None:
        return {}
    try:
        doc = json.loads(path.read_text(encoding="utf-8-sig"))
    except (json.JSONDecodeError, OSError):
        return {}
    scenarios = doc.get("scenarios")
    if not isinstance(scenarios, list):
        return {}
    out: dict[str, dict[str, Any]] = {}
    for s in scenarios:
        if not isinstance(s, dict):
            continue
        sid = str(s.get("id", "") or "")
        if not sid:
            continue
        out[sid] = s
    return out


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
    p.add_argument(
        "--manifest",
        type=Path,
        default=None,
        help="Optional manifest JSON with scenarios[].expected_tools_hint for coverage checks.",
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
    manifest_map = load_manifest_scenarios(args.manifest.resolve() if args.manifest else None)

    bundle: list[dict[str, Any]] = []
    md_lines: list[str] = [
        "# Live harness review bundle",
        "",
        "Qualitative notes: fill in below per scenario (human or external LLM).",
        "",
    ]

    seen_run_id_to_scenarios: dict[str, list[str]] = {}
    for scenario_id, jsonl in runs:
        rows: list[dict[str, Any]] = []
        if jsonl is not None:
            try:
                rows = load_jsonl(jsonl)
            except (json.JSONDecodeError, OSError):
                rows = []
        tools = tools_from_rows(rows)
        planning = planning_decisions(rows)
        ok = run_finished_success(rows)
        metrics = infer_run_metrics(rows)
        if metrics.get("non_progress_detected"):
            # Stop early: non-progress loop is already clear; avoid expensive context/ranking aggregation.
            ctx = []
            decision_logs = []
            ranking = summarize_decision_logs([])
        else:
            ctx = context_dumps_near(jsonl) if jsonl is not None else []
            decision_logs = decision_logs_near(jsonl) if jsonl is not None else []
            ranking = summarize_decision_logs(decision_logs)
        status = load_scenario_status(jsonl if jsonl is not None else (root / scenario_id / "run.jsonl"))
        run_id = None
        for r in rows:
            if r.get("type") == "run_started":
                run_id = str(r.get("run_id", "") or "")
                if run_id:
                    break
        if run_id:
            seen_run_id_to_scenarios.setdefault(run_id, []).append(scenario_id)
        artifact_integrity = str(status.get("artifact_integrity") or infer_artifact_integrity(rows))
        infra_status = str(status.get("infra_status") or "unknown")
        agent_status = str(status.get("agent_status") or infer_agent_status(rows, ok))
        editor_exit_code = status.get("editor_exit_code")
        expected_tools_hint = []
        expected_drop_reasons = []
        if scenario_id in manifest_map:
            raw_expected = manifest_map[scenario_id].get("expected_tools_hint", [])
            if isinstance(raw_expected, list):
                expected_tools_hint = [str(x) for x in raw_expected if isinstance(x, (str, int, float))]
            raw_drop_reasons = manifest_map[scenario_id].get("expected_drop_reasons", [])
            if isinstance(raw_drop_reasons, list):
                expected_drop_reasons = [str(x) for x in raw_drop_reasons if isinstance(x, (str, int, float))]
        called_tools_set = set(tools)
        expected_hit_count = sum(1 for t in expected_tools_hint if t in called_tools_set)
        expected_total = len(expected_tools_hint)
        expected_coverage_ok = True if expected_total == 0 else expected_hit_count >= max(1, expected_total // 2)
        all_drop_reason_set = {str(x["reason"]) for x in ranking["all_invocations"]["top_drop_reasons"]}
        expected_drop_hit_count = sum(1 for r in expected_drop_reasons if r in all_drop_reason_set)
        expected_drop_total = len(expected_drop_reasons)
        expected_drop_coverage_ok = True if expected_drop_total == 0 else expected_drop_hit_count >= expected_drop_total
        entry = {
            "scenario_id": scenario_id,
            "run_jsonl": str(jsonl),
            "run_id": run_id,
            "run_finished_success": ok,
            "row_count": len(rows),
            "tools_called": tools,
            "planning_decisions": planning,
            "artifact_integrity": artifact_integrity,
            "infra_status": infra_status,
            "agent_status": agent_status,
            "editor_exit_code": editor_exit_code,
            "normalization_applied_count": metrics.get("normalization_applied_count", 0),
            "low_confidence_search_count": metrics.get("low_confidence_search_count", 0),
            "low_confidence_max_streak": metrics.get("low_confidence_max_streak", 0),
            "tool_failure_top": metrics.get("tool_failure_top", []),
            "non_progress_detected": metrics.get("non_progress_detected", False),
            "non_progress_reason": metrics.get("non_progress_reason", []),
            "expected_tools_hint": expected_tools_hint,
            "expected_tools_hit_count": expected_hit_count,
            "expected_tools_total": expected_total,
            "expected_coverage_ok": expected_coverage_ok,
            "expected_drop_reasons": expected_drop_reasons,
            "expected_drop_reasons_hit_count": expected_drop_hit_count,
            "expected_drop_reasons_total": expected_drop_total,
            "expected_drop_reasons_ok": expected_drop_coverage_ok,
            "ranking_metrics": ranking,
            "context_window_files": [{"path": c[0], "line_count": c[1]} for c in ctx],
        }
        bundle.append(entry)

        md_lines.append(f"## {scenario_id}")
        md_lines.append("")
        md_lines.append(f"- **run.jsonl**: `{jsonl}` ({len(rows)} lines)" if jsonl is not None else "- **run.jsonl**: (missing)")
        md_lines.append(f"- **run_id**: `{run_id}`")
        md_lines.append(f"- **run_finished success**: {ok}")
        md_lines.append(f"- **artifact_integrity**: {artifact_integrity}")
        md_lines.append(f"- **infra_status**: {infra_status} (editor_exit_code={editor_exit_code})")
        md_lines.append(f"- **agent_status**: {agent_status}")
        md_lines.append(
            f"- **normalization_applied_count**: {metrics.get('normalization_applied_count', 0)} "
            f"(tool_failure_top={metrics.get('tool_failure_top', [])[:2]})"
        )
        md_lines.append(
            f"- **low_confidence_search_count/max_streak**: {metrics.get('low_confidence_search_count', 0)}/"
            f"{metrics.get('low_confidence_max_streak', 0)}"
        )
        if metrics.get("non_progress_detected"):
            md_lines.append(
                f"- **Non-progress loop detected**: {', '.join(metrics.get('non_progress_reason', []) or [])}"
            )
        if planning:
            last_plan = planning[-1]
            md_lines.append(
                f"- **planning_mode_used**: {last_plan.get('mode_used')} "
                f"(replan_count={last_plan.get('replan_count')}, queue_steps_pending={last_plan.get('queue_steps_pending')})"
            )
            md_lines.append(f"- **planning_trigger_reasons**: {', '.join(last_plan.get('trigger_reasons', [])) or '(none)'}")
        if expected_total > 0:
            md_lines.append(
                f"- **expected_tools_coverage**: {expected_hit_count}/{expected_total} "
                f"({'pass' if expected_coverage_ok else 'fail'})"
            )
        if expected_drop_total > 0:
            md_lines.append(
                f"- **expected_drop_reasons**: {expected_drop_hit_count}/{expected_drop_total} "
                f"({'pass' if expected_drop_coverage_ok else 'fail'})"
            )
        md_lines.append(f"- **Tools (tool_start order)**: {', '.join(tools) if tools else '(none)'}")
        md_lines.append(
            f"- **Ranking metrics (request_build)**: decision_logs={ranking['log_file_count']} kept={ranking['kept_total']} dropped={ranking['dropped_total']}"
        )
        md_lines.append(
            f"- **Ranking metrics (all invocations)**: kept={ranking['all_invocations']['kept_total']} dropped={ranking['all_invocations']['dropped_total']}"
        )
        if ranking["top_drop_reasons"]:
            top_reasons = ", ".join(
                f"{x['reason']} ({x['count']})" for x in ranking["top_drop_reasons"][:3]
            )
            md_lines.append(f"- **Top drop reasons**: {top_reasons}")
        if ranking["warnings"]:
            for w in ranking["warnings"]:
                md_lines.append(f"- **Ranking warning**: {w}")
        if ctx:
            for cpath, ln in ctx:
                md_lines.append(f"- **Context dump**: `{cpath}` ({ln} lines)")
        else:
            md_lines.append("- **Context dumps**: (none — set `UNREAL_AI_HARNESS_DUMP_CONTEXT=1` or `dumpcontext` on RunAgentTurn)")
        md_lines.append("")
        md_lines.append("### Qualitative checklist")
        md_lines.append("")
        md_lines.append("- [ ] Meets expected outcome for this scenario (`source_task` in manifest)")
        md_lines.append("- [ ] Notes:")
        md_lines.append("")
        md_lines.append("---")
        md_lines.append("")

    # Mark run_id collisions as stale duplicates to make scenario/run contamination explicit.
    duplicate_ids = {rid for rid, scenarios in seen_run_id_to_scenarios.items() if rid and len(scenarios) > 1}
    if duplicate_ids:
        for entry in bundle:
            rid = str(entry.get("run_id") or "")
            if rid in duplicate_ids and entry.get("artifact_integrity") == "ok":
                entry["artifact_integrity"] = "stale_duplicate"

    review_json = out_dir / "review.json"
    review_md = out_dir / "review.md"
    review_json.write_text(json.dumps({"scenarios": bundle}, indent=2), encoding="utf-8")
    review_md.write_text("\n".join(md_lines).rstrip() + "\n", encoding="utf-8")
    print(f"Wrote {review_json}")
    print(f"Wrote {review_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
