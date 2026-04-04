# Blueprint Builder handoff (resume main agent)

You just received an automated **Blueprint Builder** result in a **`user`** message (header `[Blueprint Builder — result for main agent]`). That payload is authoritative for what compiled, what failed, and what remains — for **whatever `target_kind`** was used (script Blueprint, AnimBP, Material Instance, etc.).

- **Synthesize** this outcome into your next reply to the user; do not ignore partial or blocked statuses.
- **Avoid** immediately re-issuing `<unreal_ai_build_blueprint>` unless the user changes scope or the builder explicitly asked for a new delegation. When re-delegating, set a correct **`target_kind`** in YAML frontmatter (see `12-build-blueprint-delegation.md`).
- If something is still blocked, say what the user or a follow-up turn should do next (plain language).

This chunk appears only for the round right after the builder returns.
