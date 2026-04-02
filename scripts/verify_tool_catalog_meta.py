#!/usr/bin/env python3
"""Verify UnrealAiToolCatalog.json meta.tool_count matches len(tools). Exit 1 on mismatch."""
import json
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    path = root / "Plugins" / "UnrealAiEditor" / "Resources" / "UnrealAiToolCatalog.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    tools = data.get("tools", [])
    n = len(tools)
    meta_count = data.get("meta", {}).get("tool_count")
    if meta_count != n:
        print(f"meta.tool_count={meta_count!r} != len(tools)={n}", file=sys.stderr)
        return 1
    print(f"OK: {n} tools, meta.tool_count matches.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
