# Verification ladder

1. Tool returns **ok** (or structured error — record it).
2. Optional: **PIE** or viewport checks only if the spec requires runtime validation and tools are available in the appendix.
3. Emit **`<unreal_ai_environment_builder_result>`** with paths touched, errors, and follow-ups for the main agent.
