#!/usr/bin/env python3
"""Load UnrealAiToolCatalog.json and merge meta.tool_catalog_fragments into tools list (same rules as C++ loader)."""
from __future__ import annotations

import json
import sys
from pathlib import Path


def _load_fragment_tools(resources_dir: Path, spec, repo_root: Path) -> list[tuple[dict, str | None]]:
    """Return list of (tool_obj, retrieval_bundle or None)."""
    out: list[tuple[dict, str | None]] = []
    if isinstance(spec, str):
        rel = spec.strip()
        bundle = None
    elif isinstance(spec, dict):
        rel = (spec.get("path") or spec.get("relative_path") or "").strip()
        bundle = spec.get("retrieval_bundle")
        if isinstance(bundle, str):
            bundle = bundle.strip() or None
        else:
            bundle = None
    else:
        return out
    if not rel:
        return out
    frag_path = (resources_dir / rel).resolve()
    try:
        frag_path.relative_to(resources_dir.resolve())
    except ValueError:
        print(f"fragment path escapes Resources: {rel}", file=sys.stderr)
        return out
    if not frag_path.is_file():
        print(f"missing fragment file: {frag_path}", file=sys.stderr)
        return out
    data = json.loads(frag_path.read_text(encoding="utf-8"))
    tools = data.get("tools")
    if not isinstance(tools, list):
        print(f"fragment missing tools[]: {frag_path}", file=sys.stderr)
        return out
    for t in tools:
        if isinstance(t, dict):
            out.append((t, bundle))
    return out


def load_merged_catalog(repo_root: Path | None = None) -> dict:
    root = repo_root or Path(__file__).resolve().parents[1]
    path = root / "Plugins" / "UnrealAiEditor" / "Resources" / "UnrealAiToolCatalog.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    resources_dir = path.parent
    tools_by_id: dict[str, dict] = {}
    for t in data.get("tools", []):
        if not isinstance(t, dict):
            continue
        tid = t.get("tool_id")
        if isinstance(tid, str) and tid:
            tools_by_id[tid] = t
    frags = data.get("meta", {}).get("tool_catalog_fragments") or []
    if isinstance(frags, list):
        for spec in frags:
            for frag_tool, bundle in _load_fragment_tools(resources_dir, spec, root):
                tid = frag_tool.get("tool_id")
                if not isinstance(tid, str) or not tid:
                    continue
                if bundle and "retrieval_bundle" not in frag_tool:
                    frag_tool = {**frag_tool, "retrieval_bundle": bundle}
                tools_by_id[tid] = frag_tool
    merged_tools = [tools_by_id[k] for k in sorted(tools_by_id.keys())]
    data["tools"] = merged_tools
    if "meta" in data and isinstance(data["meta"], dict):
        data["meta"]["tool_count"] = len(merged_tools)
    return data


def main() -> int:
    m = load_merged_catalog()
    n = len(m.get("tools", []))
    print(f"Merged catalog: {n} tools (including fragments).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
