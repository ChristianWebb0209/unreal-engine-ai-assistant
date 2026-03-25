# Unreal Blueprint Formatter — dependency on Unreal AI Editor

Unreal AI Editor **links** the **`UnrealBlueprintFormatter`** editor module (`UnrealAiEditor.Build.cs`) and declares the plugin in `UnrealAiEditor.uplugin`.

**Canonical source repository:** [github.com/ChristianWebb0209/unreal-engine-blueprint-plugin](https://github.com/ChristianWebb0209/unreal-engine-blueprint-plugin)

The formatter copy under `Plugins/UnrealBlueprintFormatter/` is **not** tracked in the Unreal AI plugin repo (see root `.gitignore`). **`build-editor.ps1`** clones or `git pull --ff-only` that folder before each build so you always compile against the latest formatter `main` branch.

## What you need to build

1. **Git** on `PATH` (for clone/pull).
2. Run **`.\build-editor.ps1`** from the project root — it updates `Plugins/UnrealBlueprintFormatter` then builds.
3. First run: clones `https://github.com/ChristianWebb0209/unreal-engine-blueprint-plugin.git` into `Plugins/UnrealBlueprintFormatter` if that path is missing or not a git repo.
4. If `Plugins/UnrealBlueprintFormatter` exists but is **not** a clone (no `.git`), delete or rename it so the script can clone.

### Skip sync (offline / fork)

- **`.\build-editor.ps1 -SkipBlueprintFormatterSync`**
- Or set **`UE_SKIP_BLUEPRINT_FORMATTER_SYNC=1`**

### Optional: manual clone

```bash
git clone https://github.com/ChristianWebb0209/unreal-engine-blueprint-plugin.git Plugins/UnrealBlueprintFormatter
```

## Without the formatter

Layout and `blueprint_format_graph` **degrade gracefully**: `blueprint_apply_ir` still applies IR and returns `formatter_hint` when the module is missing. You cannot remove the **compile-time** module dependency without editing `UnrealAiEditor.Build.cs` and bridging code to make the formatter optional (not the default in this repo).
