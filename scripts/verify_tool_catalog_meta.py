#!/usr/bin/env python3
"""Verify merged catalog meta.tool_count matches len(tools) after tool_catalog_fragments merge."""
import sys
from pathlib import Path

# Allow `from unreal_ai_tool_catalog_merge import ...` when run as script
sys.path.insert(0, str(Path(__file__).resolve().parent))

from unreal_ai_tool_catalog_merge import load_merged_catalog  # noqa: E402


def main() -> int:
    m = load_merged_catalog()
    tools = m.get("tools", [])
    n = len(tools)
    meta_count = m.get("meta", {}).get("tool_count")
    if meta_count != n:
        print(f"meta.tool_count={meta_count!r} != len(merged tools)={n}", file=sys.stderr)
        return 1
    seen: set[str] = set()
    for t in tools:
        if not isinstance(t, dict):
            print("tool entry is not an object", file=sys.stderr)
            return 1
        tid = t.get("tool_id")
        if not tid or not isinstance(tid, str):
            print("tool entry missing tool_id", file=sys.stderr)
            return 1
        if tid in seen:
            print(f"Duplicate tool_id after merge: {tid}", file=sys.stderr)
            return 1
        seen.add(tid)
        if not isinstance(t.get("parameters"), dict):
            print(f"Tool {tid} missing parameters object.", file=sys.stderr)
            return 1
    print(f"OK: {n} tools after merge, meta.tool_count matches.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
