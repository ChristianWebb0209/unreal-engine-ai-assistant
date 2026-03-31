import math
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


ISO_8601_UTC_Z_RE = re.compile(r"^(?P<body>.+)Z$")


def parse_iso_z(s: str):
    # Example: 2026-03-30T22:54:39.064Z
    s = s.strip()
    m = ISO_8601_UTC_Z_RE.match(s)
    if m:
        s = m.group("body") + "+00:00"
    # Python can parse "+00:00"
    from datetime import datetime

    return datetime.fromisoformat(s)


run_start_re = re.compile(
    r"\[(?P<ts>[^\]]+)\]\s*RunAgentTurnSync:.*?mode=(?P<mode>\w+)"
)
http_round_re = re.compile(
    r"\[(?P<ts>[^\]]+)\]\s*HTTP outbound:.*?mode=(?P<mode>\w+)\s+llm_round=(?P<round>\d+)"
)
finish_re = re.compile(
    r"\[(?P<ts>[^\]]+)\]\s*OnRunFinished\s*\(terminal\)\s*success=(?P<succ>yes|no)"
)


@dataclass(frozen=True)
class TurnRow:
    path: str
    mode: str
    duration_s: float
    llm_round_max: int
    llm_round_unique: int
    success: bool


def pct(values, p: float):
    if not values:
        return float("nan")
    values = sorted(values)
    n = len(values)
    k = (n - 1) * p
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return values[int(k)]
    return values[f] * (c - k) + values[c] * (k - f)


def main():
    root = Path("tests")
    log_paths = list(root.glob("**/harness_progress.log"))
    print(f"Found harness_progress.log files: {len(log_paths)}")

    rows: list[TurnRow] = []

    for p in log_paths:
        try:
            txt = p.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            continue

        m = run_start_re.search(txt)
        if not m:
            continue
        mode = m.group("mode").strip()
        start_ts = parse_iso_z(m.group("ts"))

        mf = finish_re.search(txt)
        if not mf:
            continue
        succ = mf.group("succ") == "yes"
        finish_ts = parse_iso_z(mf.group("ts"))

        rounds = set()
        for mm in http_round_re.finditer(txt):
            if mm.group("mode").strip() == mode:
                rounds.add(int(mm.group("round")))

        duration_s = (finish_ts - start_ts).total_seconds()
        llm_round_max = max(rounds) if rounds else 0
        llm_round_unique = len(rounds)

        rows.append(
            TurnRow(
                path=str(p),
                mode=mode,
                duration_s=duration_s,
                llm_round_max=llm_round_max,
                llm_round_unique=llm_round_unique,
                success=succ,
            )
        )

    print(f"Parsed turns: {len(rows)}")
    by_mode: dict[str, list[TurnRow]] = defaultdict(list)
    for r in rows:
        by_mode[r.mode].append(r)

    for mode in ["plan", "agent", "ask"]:
        arr = by_mode.get(mode, [])
        if not arr:
            print(f"MODE {mode}: no data")
            continue
        durations = [r.duration_s for r in arr]
        mean = sum(durations) / len(durations)
        med = pct(durations, 0.5)
        p90 = pct(durations, 0.90)
        p95 = pct(durations, 0.95)
        p99 = pct(durations, 0.99)
        avg_round_max = sum(r.llm_round_max for r in arr) / len(arr)
        avg_round_unique = sum(r.llm_round_unique for r in arr) / len(arr)
        print(
            f"MODE {mode}: n={len(arr)} "
            f"mean={mean:.1f}s median={med:.1f}s p90={p90:.1f}s p95={p95:.1f}s p99={p99:.1f}s "
            f"avg_llm_round_max={avg_round_max:.2f} avg_llm_round_unique={avg_round_unique:.2f}"
        )

    # Success-only summary
    for mode in ["plan", "agent", "ask"]:
        arr = [r for r in by_mode.get(mode, []) if r.success]
        if not arr:
            continue
        durations = [r.duration_s for r in arr]
        mean = sum(durations) / len(durations)
        med = pct(durations, 0.5)
        p90 = pct(durations, 0.90)
        print(f"MODE {mode} SUCCESS: n={len(arr)} mean={mean:.1f}s median={med:.1f}s p90={p90:.1f}s")

    # Show worst offenders per mode
    for mode in ["plan", "agent", "ask"]:
        arr = sorted(by_mode.get(mode, []), key=lambda r: r.duration_s, reverse=True)
        if not arr:
            continue
        print(f"Top slowest turns for {mode}:")
        for r in arr[:5]:
            print(
                f"  {r.duration_s:.1f}s rounds_max={r.llm_round_max} rounds_unique={r.llm_round_unique} success={r.success} file={r.path}"
            )


if __name__ == "__main__":
    main()

