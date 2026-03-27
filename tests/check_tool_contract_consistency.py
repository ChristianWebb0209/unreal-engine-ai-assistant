#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_catalog(root: Path) -> dict:
    p = root / "Plugins" / "UnrealAiEditor" / "Resources" / "UnrealAiToolCatalog.json"
    return json.loads(p.read_text(encoding="utf-8-sig"))


def collect_dispatch_tool_ids(root: Path) -> set[str]:
    dispatch = root / "Plugins" / "UnrealAiEditor" / "Source" / "UnrealAiEditor" / "Private" / "Tools" / "UnrealAiToolDispatch.cpp"
    txt = dispatch.read_text(encoding="utf-8")
    # Matches: if (ToolId == TEXT("foo")) and else if (...) variants
    return set(re.findall(r'ToolId\s*==\s*TEXT\("([^"]+)"\)', txt))


def schema_errors(tool: dict) -> list[str]:
    errs: list[str] = []
    tid = str(tool.get("tool_id") or "<unknown>")
    params = tool.get("parameters")
    if not isinstance(params, dict):
        errs.append(f"{tid}: parameters must be an object")
        return errs
    if params.get("type") != "object":
        errs.append(f"{tid}: parameters.type must be 'object'")
    for k in ("anyOf", "oneOf", "allOf", "not", "enum"):
        if k in params:
            errs.append(f"{tid}: top-level '{k}' is endpoint-incompatible")
    return errs


def main() -> int:
    root = repo_root()
    catalog = load_catalog(root)
    tools = catalog.get("tools")
    if not isinstance(tools, list):
        print("Catalog missing tools[]", file=sys.stderr)
        return 2

    ids: list[str] = []
    errors: list[str] = []
    status_by_id: dict[str, str] = {}
    for t in tools:
        if not isinstance(t, dict):
            continue
        tid = str(t.get("tool_id") or "").strip()
        if not tid:
            errors.append("Found tool without tool_id")
            continue
        ids.append(tid)
        status_by_id[tid] = str(t.get("status") or "")
        errors.extend(schema_errors(t))

    dupes = sorted({x for x in ids if ids.count(x) > 1})
    for d in dupes:
        errors.append(f"Duplicate tool_id: {d}")

    dispatch_ids = collect_dispatch_tool_ids(root)
    for tid in sorted(set(ids)):
        if tid in dispatch_ids:
            continue
        # Non-routable catalog entries may intentionally be placeholders.
        if status_by_id.get(tid, "").lower() in {"future", "experimental"}:
            continue
        errors.append(f"{tid}: in catalog but not routed in UnrealAiToolDispatch.cpp (status={status_by_id.get(tid,'')})")

    if errors:
        print("FAILED tool consistency checks:")
        for e in errors:
            print(f"- {e}")
        return 1

    print(f"OK: {len(set(ids))} tools checked, schema and routing consistency passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
