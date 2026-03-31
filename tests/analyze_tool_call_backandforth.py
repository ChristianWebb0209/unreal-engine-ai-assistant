import json
import math
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


@dataclass
class ToolCall:
    tool: str
    call_id: str
    success: bool | None


def safe_int(x, default=0):
    try:
        return int(x)
    except Exception:
        return default


def iter_run_jsonl_lines(path: Path):
    # run.jsonl is JSONL; iterate line-by-line to avoid loading huge files.
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except Exception:
                # Some harness artifacts may contain truncated lines; skip them.
                continue


def main():
    root = Path("tests")
    run_jsonl_paths = list(root.glob("**/turns/**/run.jsonl"))
    print(f"Found run.jsonl files: {len(run_jsonl_paths)}")

    # Global stats
    tool_calls_total = Counter()
    tool_calls_failed = Counter()

    # Back-and-forth patterns:
    # - adjacent_same_tool: tool X immediately followed by tool X
    # - retry_after_failure_same_tool: failed call of X followed soon by another call of X
    adjacent_same_tool = Counter()  # keyed by tool
    retry_after_failure_same_tool = Counter()  # keyed by tool
    max_consecutive_streak = Counter()  # keyed by tool

    # More global loop pattern: A -> B -> A triples
    triple_aba = Counter()  # keyed by (A,B)

    # Track a few examples for reporting
    examples = {
        "adjacent_same_tool": defaultdict(list),  # tool -> list of file paths
        "retry_after_failure_same_tool": defaultdict(list),  # tool -> list of file paths
        "triple_aba": defaultdict(list),  # (A,B) -> list of file paths
    }

    turns_processed = 0
    turns_skipped_no_events = 0

    for p in run_jsonl_paths:
        tool_seq: list[ToolCall] = []
        call_id_to_index: dict[str, int] = {}
        saw_tool_start = False

        for obj in iter_run_jsonl_lines(p):
            if obj.get("type") == "tool_start":
                saw_tool_start = True
                tool = obj.get("tool")
                call_id = obj.get("call_id")
                if not tool or not call_id:
                    continue
                idx = len(tool_seq)
                tool_seq.append(ToolCall(tool=str(tool), call_id=str(call_id), success=None))
                call_id_to_index[str(call_id)] = idx
            elif obj.get("type") == "tool_finish":
                call_id = obj.get("call_id")
                if call_id is None:
                    continue
                idx = call_id_to_index.get(str(call_id))
                if idx is None:
                    continue
                success = obj.get("success")
                if success is None:
                    continue
                tool_seq[idx].success = bool(success)

        if not saw_tool_start:
            continue

        turns_processed += 1
        if not tool_seq:
            turns_skipped_no_events += 1
            continue

        # Per-turn stats
        tools_only = [tc.tool for tc in tool_seq]

        # Total tool call stats
        for tc in tool_seq:
            tool_calls_total[tc.tool] += 1
            if tc.success is False:
                tool_calls_failed[tc.tool] += 1

        # Adjacent same-tool streaks
        streak_tool = tools_only[0]
        streak_len = 1
        for i in range(1, len(tools_only)):
            if tools_only[i] == streak_tool:
                streak_len += 1
            else:
                if streak_len > 1:
                    max_consecutive_streak[streak_tool] = max(
                        max_consecutive_streak[streak_tool], streak_len
                    )
                streak_tool = tools_only[i]
                streak_len = 1
        if streak_len > 1:
            max_consecutive_streak[streak_tool] = max(
                max_consecutive_streak[streak_tool], streak_len
            )

        for i in range(1, len(tool_seq)):
            if tool_seq[i - 1].tool == tool_seq[i].tool:
                adjacent_same_tool[tool_seq[i].tool] += 1
                if len(examples["adjacent_same_tool"][tool_seq[i].tool]) < 3:
                    examples["adjacent_same_tool"][tool_seq[i].tool].append(str(p))

        # Retry-after-failure within next 3 tool calls
        # (captures A failing then quickly repeating A)
        window = 3
        for i, tc in enumerate(tool_seq):
            if tc.success is not False:
                continue
            tool = tc.tool
            end = min(len(tool_seq), i + 1 + window)
            seen = False
            for j in range(i + 1, end):
                if tool_seq[j].tool == tool:
                    retry_after_failure_same_tool[tool] += 1
                    seen = True
                    if len(examples["retry_after_failure_same_tool"][tool]) < 3:
                        examples["retry_after_failure_same_tool"][tool].append(str(p))
                    break
            if not seen:
                # no immediate repeat of same tool
                pass

        # A -> B -> A patterns (tool_seq[i]==tool_seq[i+2])
        for i in range(0, len(tool_seq) - 2):
            a = tool_seq[i].tool
            b = tool_seq[i + 1].tool
            if tool_seq[i + 2].tool == a:
                triple_aba[(a, b)] += 1
                if len(examples["triple_aba"][(a, b)]) < 2:
                    examples["triple_aba"][(a, b)].append(str(p))

    print(f"Turns processed (run.jsonl w/ tool_start): {turns_processed}")
    if turns_skipped_no_events:
        print(f"Skipped empty tool sequences: {turns_skipped_no_events}")

    # Derived rates
    def fail_rate(tool: str) -> float:
        total = tool_calls_total.get(tool, 0)
        if total == 0:
            return 0.0
        return tool_calls_failed.get(tool, 0) / total

    # Report top offenders
    def top_counter(counter: Counter, n=12):
        return counter.most_common(n)

    print("\n=== Top tools by adjacent same-tool repeats (A then A) ===")
    for tool, cnt in top_counter(adjacent_same_tool, n=12):
        print(
            f"- {tool}: adjacent_repeat={cnt}, calls={tool_calls_total[tool]}, "
            f"fail_rate={fail_rate(tool):.2%}, max_streak={max_consecutive_streak.get(tool,1)}"
        )
        ex = examples["adjacent_same_tool"].get(tool, [])[:1]
        if ex:
            print(f"  example: {ex[0]}")

    print("\n=== Top tools by retry-after-failure (failed X then soon X) ===")
    for tool, cnt in top_counter(retry_after_failure_same_tool, n=12):
        f = fail_rate(tool)
        print(
            f"- {tool}: retry_after_failure={cnt}, calls={tool_calls_total[tool]}, "
            f"fail_rate={f:.2%}, max_streak={max_consecutive_streak.get(tool,1)}"
        )
        ex = examples["retry_after_failure_same_tool"].get(tool, [])[:1]
        if ex:
            print(f"  example: {ex[0]}")

    print("\n=== Top A->B->A tool loops (often discovery/mis-discovery oscillations) ===")
    for (a, b), cnt in triple_aba.most_common(10):
        print(f"- {a} -> {b} -> {a}: count={cnt}")
        ex = examples["triple_aba"].get((a, b), [])[:1]
        if ex:
            print(f"  example: {ex[0]}")

    print("\nDone.")


if __name__ == "__main__":
    main()

