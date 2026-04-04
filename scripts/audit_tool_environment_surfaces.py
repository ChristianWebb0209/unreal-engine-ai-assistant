#!/usr/bin/env python3
"""
Audit merged tool catalog for Environment / PCG Builder agent_surfaces alignment.

Rules (Agent uses bOmitMainAgentBlueprintMutationTools + agent_surfaces):
- Tools in category landscape_foliage_pcg with permission "write" must declare
  agent_surfaces containing "environment_builder" (case-insensitive), and must
  not list both "main_agent" and "environment_builder" (same rule as blueprint mutators).

Run from repo root: python scripts/audit_tool_environment_surfaces.py
Exit 1 on violation.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from unreal_ai_tool_catalog_merge import load_merged_catalog  # noqa: E402


def norm_surfaces(raw) -> list[str] | None:
    if raw is None:
        return None
    if not isinstance(raw, list):
        return None
    return [str(x).strip().lower() for x in raw if str(x).strip()]


def main() -> int:
    doc = load_merged_catalog()
    tools = doc.get("tools") or []
    errors: list[str] = []

    for t in tools:
        if t.get("category") != "landscape_foliage_pcg":
            continue
        tid = t.get("tool_id")
        if not tid:
            continue
        perm = t.get("permission")
        if perm != "write":
            continue
        surf = norm_surfaces(t.get("agent_surfaces"))
        if not surf:
            errors.append(
                f"{tid}: category=landscape_foliage_pcg permission=write but agent_surfaces missing/empty "
                f"(implicit all) — use [\"environment_builder\"] for PCG/landscape/foliage mutators."
            )
            continue
        if "main_agent" in surf and "environment_builder" in surf:
            errors.append(
                f"{tid}: agent_surfaces lists both main_agent and environment_builder — pick one pipeline."
            )
        if "environment_builder" not in surf:
            errors.append(
                f"{tid}: permission=write landscape_foliage_pcg but agent_surfaces={t.get('agent_surfaces')!r} "
                f"omits environment_builder."
            )

    if errors:
        print("audit_tool_environment_surfaces: FAIL", file=sys.stderr)
        for e in errors:
            print(e, file=sys.stderr)
        return 1

    print(f"audit_tool_environment_surfaces: OK ({len(tools)} tools merged).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
