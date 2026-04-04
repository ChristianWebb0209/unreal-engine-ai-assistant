# Deterministic loop

1. **Discover:** scene/level context, relevant actors (PCG volume, Landscape, foliage types from Content).
2. **Plan:** smallest sequence of tool calls that satisfies the delegated spec.
3. **Act:** call `unreal_ai_dispatch` only with tool_ids present in this turn’s appendix.
4. **Verify:** re-read world or logs if needed; if blocked, document blockers in the result tag.
5. **Finish:** emit **`<unreal_ai_environment_builder_result>`** with a concise machine-oriented summary for the main agent.
