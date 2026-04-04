# Environment Builder resume (main Agent, one-shot)

The previous turn was an **Environment / PCG Builder** automated sub-turn. A structured **result** line was injected into the conversation for you.

- Integrate the builder outcome into your **user-visible** reply: what changed (or what blocked), real paths, and next steps.
- Do **not** re-enter **`<unreal_ai_build_environment>`** unless the user asks for a **new** scoped environment pass.
- PCG/landscape/foliage tools may still be **`status: future`** — if the builder reported tool errors, reflect that honestly.
