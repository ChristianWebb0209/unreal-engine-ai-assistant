# UE5 editor performance (this machine)

## Where settings live

| Scope | Path (Windows) |
|--------|----------------|
| **Per engine version (your UE 5.7)** | `%LOCALAPPDATA%\UnrealEngine\5.7\Saved\Config\WindowsEditor\` |
| **Other engine versions** | `%LOCALAPPDATA%\UnrealEngine\5.5\`, `5.6`, etc. |
| **This project** | `<repo>\Saved\Config\WindowsEditor\` (merged with the above) |

Important files:

- `Editor.ini` — `[ConsoleVariables]` for editor startup CVars
- `EditorSettings.ini` — `EditorPerformanceSettings`, `ScalabilityGroups`, Zen, etc.
- `EditorPerProjectUserSettings.ini` — large per-user prefs (layout, Niagara, etc.)

## What was tuned (high level)

- **Scalability groups** set to **0** (lowest) for editor.
- **Viewport render scale** ~**50%** via `ManualScreenPercentage` (biggest win; looks soft).
- **High-DPI** editor viewports off, **VSync** off for editor.
- **Telemetry** off, scalability warning **indicator** off.
- **Console variables**: lower shadows, distance, PP, AA; GI/reflection methods set to **None** in editor (cheap).
- **Niagara sequencer**: `bActivateRealtimeViewports=False` (less live preview).
- **Grid**: anti-aliased grid off.

## Revert

1. Delete or edit `Saved\Config\WindowsEditor\Editor.ini` and `EditorSettings.ini` in this repo.
2. In `%LOCALAPPDATA%\UnrealEngine\5.7\Saved\Config\WindowsEditor\`, remove the `[ConsoleVariables]` block from `Editor.ini` and restore `EditorSettings.ini` / `EditorPerProjectUserSettings.ini` from backup or Epic defaults (re-launch editor may regenerate some keys).

## If you use UE 5.5 as well

Copy the same `[ConsoleVariables]` and `EditorSettings.ini` sections into `%LOCALAPPDATA%\UnrealEngine\5.5\Saved\Config\WindowsEditor\` or only use one engine version for this project (`blank.uproject` → `EngineAssociation`).
