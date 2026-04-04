# PCG in Unreal (authoring mental model)

- PCG graphs are assets; **PCG components** live on actors in the level. **`pcg_generate`** targets an **actor** path hosting a `UPCGComponent`.
- Execution is **editor-world** stateful: prefer explicit actor paths from discovery tools, not guesses.
- If the tool returns **not implemented**, state that in the builder result — do not pretend success.
