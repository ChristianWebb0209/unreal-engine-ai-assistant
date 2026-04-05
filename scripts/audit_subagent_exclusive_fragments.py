#!/usr/bin/env python3
"""
Ensure exclusive agent_surfaces are stored in the correct catalog file:
  - blueprint_builder-only -> tools.blueprint.json (fragment)
  - environment_builder-only -> tools.environment.json (fragment)
  - main_agent-only -> tools.main.json primary tools[] (not in fragment files)

Primary document: tools.main.json (meta + main roster; same merge rules as C++ loader).

Run from repo root: python scripts/audit_subagent_exclusive_fragments.py
Exit 1 on violation.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from unreal_ai_tool_catalog_merge import load_merged_catalog  # noqa: E402

PRIMARY_CATALOG_REL = "tools.main.json"
EXPECTED_FRAGMENT_PATH_BY_BUNDLE = {
    "environment_builder": "tools.environment.json",
    "blueprint_builder": "tools.blueprint.json",
}
FRAGMENT_EXCLUSIVE_BUNDLES = frozenset(EXPECTED_FRAGMENT_PATH_BY_BUNDLE.keys())


def norm_surfaces(raw) -> list[str] | None:
    if raw is None:
        return None
    if not isinstance(raw, list):
        return None
    out = [str(x).strip().lower() for x in raw if str(x).strip()]
    return out if out else None


def load_fragment_specs(resources_dir: Path, catalog: dict) -> list[tuple[str, str, set[str]]]:
    """Return list of (retrieval_bundle_lower, rel_path, set of tool_id)."""
    meta = catalog.get("meta") or {}
    frags = meta.get("tool_catalog_fragments") or []
    if not isinstance(frags, list):
        return []
    rows: list[tuple[str, str, set[str]]] = []
    for spec in frags:
        rel = ""
        bundle = ""
        if isinstance(spec, str):
            rel = spec.strip()
        elif isinstance(spec, dict):
            rel = (spec.get("path") or spec.get("relative_path") or "").strip()
            bundle = (spec.get("retrieval_bundle") or "").strip()
        if not rel or not bundle:
            continue
        b = bundle.lower()
        frag_path = resources_dir / rel
        if not frag_path.is_file():
            raise FileNotFoundError(f"fragment file missing: {frag_path}")
        data = json.loads(frag_path.read_text(encoding="utf-8"))
        tools = data.get("tools")
        if not isinstance(tools, list):
            raise ValueError(f"fragment {rel} missing tools[]")
        ids: set[str] = set()
        for t in tools:
            if isinstance(t, dict) and isinstance(t.get("tool_id"), str) and t["tool_id"]:
                ids.add(t["tool_id"])
        rows.append((b, rel, ids))
    return rows


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    catalog_path = root / "Plugins" / "UnrealAiEditor" / "Resources" / PRIMARY_CATALOG_REL
    resources_dir = catalog_path.parent
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    primary_tool_ids: set[str] = set()
    for t in catalog.get("tools") or []:
        if isinstance(t, dict) and isinstance(t.get("tool_id"), str) and t["tool_id"]:
            primary_tool_ids.add(t["tool_id"])

    merged = load_merged_catalog()
    tools_merged = merged.get("tools") or []
    by_id_merged = {t["tool_id"]: t for t in tools_merged if isinstance(t, dict) and t.get("tool_id")}

    errors: list[str] = []

    try:
        frag_rows = load_fragment_specs(resources_dir, catalog)
    except (OSError, ValueError, json.JSONDecodeError) as e:
        print(f"audit_subagent_exclusive_fragments: FAIL ({e})", file=sys.stderr)
        return 1

    bundle_to_ids: dict[str, set[str]] = {}
    for b, rel, ids in frag_rows:
        if b in FRAGMENT_EXCLUSIVE_BUNDLES:
            bundle_to_ids[b] = ids
            expected = EXPECTED_FRAGMENT_PATH_BY_BUNDLE.get(b)
            if expected and rel != expected:
                errors.append(
                    f"fragment for retrieval_bundle={b!r} uses path {rel!r}; expected {expected!r}."
                )

    for t in tools_merged:
        if not isinstance(t, dict):
            continue
        tid = t.get("tool_id")
        if not tid:
            continue
        surf = norm_surfaces(t.get("agent_surfaces"))
        if surf == ["environment_builder"]:
            if tid not in bundle_to_ids.get("environment_builder", set()):
                errors.append(
                    f"{tid}: agent_surfaces=['environment_builder'] but tool_id not listed in "
                    f"{EXPECTED_FRAGMENT_PATH_BY_BUNDLE['environment_builder']}."
                )
        elif surf == ["blueprint_builder"]:
            if tid not in bundle_to_ids.get("blueprint_builder", set()):
                errors.append(
                    f"{tid}: agent_surfaces=['blueprint_builder'] but tool_id not listed in "
                    f"{EXPECTED_FRAGMENT_PATH_BY_BUNDLE['blueprint_builder']}."
                )
        elif surf == ["main_agent"]:
            if tid not in primary_tool_ids:
                errors.append(
                    f"{tid}: agent_surfaces=['main_agent'] only must be defined in {PRIMARY_CATALOG_REL} tools[], "
                    f"not only in a fragment."
                )

    for b, rel, ids in frag_rows:
        if b not in FRAGMENT_EXCLUSIVE_BUNDLES:
            continue
        for tid in ids:
            defn = by_id_merged.get(tid)
            if not defn:
                errors.append(f"fragment {rel}: tool_id {tid!r} missing from merged catalog.")
                continue
            surf = norm_surfaces(defn.get("agent_surfaces"))
            want = [b]
            if surf != want:
                errors.append(
                    f"{tid}: in {rel} but merged agent_surfaces={defn.get('agent_surfaces')!r} "
                    f"(expected {want!r})."
                )

    main_tools = catalog.get("tools") or []
    if isinstance(main_tools, list):
        for t in main_tools:
            if not isinstance(t, dict):
                continue
            tid = t.get("tool_id")
            surf = norm_surfaces(t.get("agent_surfaces"))
            if surf and len(surf) == 1 and surf[0] in FRAGMENT_EXCLUSIVE_BUNDLES:
                errors.append(
                    f"{tid}: exclusive agent_surfaces={surf!r} must live in fragment "
                    f"({EXPECTED_FRAGMENT_PATH_BY_BUNDLE[surf[0]]}), not in {PRIMARY_CATALOG_REL} tools[]."
                )

    if errors:
        print("audit_subagent_exclusive_fragments: FAIL", file=sys.stderr)
        for e in errors:
            print(e, file=sys.stderr)
        return 1

    print(
        f"audit_subagent_exclusive_fragments: OK ({len(tools_merged)} merged tools; "
        f"blueprint={len(bundle_to_ids.get('blueprint_builder', ()))}, "
        f"environment={len(bundle_to_ids.get('environment_builder', ()))}; "
        f"primary roster={len(primary_tool_ids)})."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
