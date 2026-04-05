#!/usr/bin/env python3
"""
Audit tools.main.json (merged with fragments) for Main Agent vs Blueprint Builder alignment.

Rules (default Agent uses bOmitMainAgentBlueprintMutationTools + agent_surfaces):
- Blueprint-family tools with permission "write" must either declare
  agent_surfaces containing "blueprint_builder" (case-insensitive), or be listed
  in ALLOW_IMPLICIT_ALL_WRITE (UI-only / non-mutating graph open).
- Declared agent_surfaces must not list both "main_agent" and "blueprint_builder"
  for the same tool (would re-expose mutators on the main roster).

Run from repo root: python scripts/audit_tool_agent_surfaces.py
Exit 1 on violation.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from unreal_ai_tool_catalog_merge import load_merged_catalog  # noqa: E402


ALLOW_IMPLICIT_ALL_WRITE = frozenset(
    {
        # Opens editor / graph tab; not graph mutation IR/patch.
        "blueprint_open_graph_tab",
    }
)


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
        tid = t.get("tool_id")
        if not tid or not str(tid).startswith("blueprint_"):
            continue
        perm = t.get("permission")
        if perm != "write":
            continue
        if tid in ALLOW_IMPLICIT_ALL_WRITE:
            continue
        surf = norm_surfaces(t.get("agent_surfaces"))
        if not surf:
            errors.append(
                f"{tid}: permission=write but agent_surfaces missing/empty "
                f"(implicit all) — use [\"blueprint_builder\"] for graph mutators or add to ALLOW_IMPLICIT_ALL_WRITE."
            )
            continue
        if "main_agent" in surf and "blueprint_builder" in surf:
            errors.append(
                f"{tid}: agent_surfaces lists both main_agent and blueprint_builder — pick one pipeline."
            )
        if "blueprint_builder" not in surf:
            errors.append(
                f"{tid}: permission=write but agent_surfaces={t.get('agent_surfaces')!r} "
                f"omits blueprint_builder (main agent would retain this tool when surfaces are explicit)."
            )

    if errors:
        print("audit_tool_agent_surfaces: FAIL", file=sys.stderr)
        for e in errors:
            print(e, file=sys.stderr)
        return 1

    implicit_all = sum(
        1
        for t in tools
        if t.get("tool_id") and norm_surfaces(t.get("agent_surfaces")) is None
    )
    print(
        f"audit_tool_agent_surfaces: OK ({len(tools)} tools; "
        f"{implicit_all} with implicit agent_surfaces=all)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
