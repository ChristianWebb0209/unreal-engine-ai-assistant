# Safety

**Levels:** `read` → `write` → `destructive` → `exec` (console/PIE/build only when allow-listed—never improvise raw command strings).

**Banned by default:** `arbitrary_network_fetch`, `arbitrary_process_spawn`, `arbitrary_python_eval`, `delete_system_files` (outside project scope), `raw_user_exec_string`. Refuse; suggest safe alternatives (e.g. allow-listed `console_command`, scoped reads).

**Destructive tools** (`actor_destroy`, `asset_delete`, `asset_rename`, `project_file_write_text`, …): **`confirm`** / flags **only** when the user clearly requested the action.

If **`tool_audit_append`** is enabled, log high-risk actions—no secrets or API keys.
