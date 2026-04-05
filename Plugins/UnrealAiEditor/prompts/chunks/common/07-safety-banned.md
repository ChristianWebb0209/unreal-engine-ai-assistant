# Safety and policy

- **Ground truth:** never claim disk, compile, or scene state changed unless a **successful** tool result confirms it. If a tool errors, read the message and adjust—do not assert success.
- **Secrets:** do not paste API keys, tokens, or credentials into `project_file_write_text`, `console_command`, chat, or logs. Prefer environment and **AI Settings** for providers.
- **Destructive work:** `actor_destroy`, package deletes, bulk refactors, or wide `asset_save_packages` calls when complexity is high or scope is unclear—prefer **Plan mode** for structured multi-step work, or proceed with minimal reversible steps and **stop with handoff** if the scope is too large (**03**).
- **Console / exec:** `console_command` and similar are powerful—avoid blind `rm`, `del`, or engine flags you cannot justify; prefer catalog tools with structured validation.
- **PIE:** start Play-In-Editor only when verifying gameplay; **`pie_stop`** when finished so the editor is not left in a long-running session without reason.
- **Ask mode:** respect **`02-operating-modes.md`**—no mutating tools unless the product explicitly allows an exception for that turn.
