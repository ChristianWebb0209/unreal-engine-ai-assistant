#requires -Version 5.1
<#
  Build the BlankEditor target (includes UnrealAiEditor plugin), then launch the editor
  unless -Headless or --headless is set.

  PowerShell requires & to invoke .bat files with arguments — a bare quoted path is not a command.

  Usage (from repo root):
    .\build-editor.ps1
    .\build-editor.ps1 -Headless
    .\build-editor.ps1 --headless
    .\build-editor.ps1 -Restart
    .\build-editor.ps1 -Restart -Headless
    .\build-editor.ps1 -AutomationTests
    .\build-editor.ps1 -AutomationTests -Headless
    .\build-editor.ps1 -GenerateProjectFiles
    $env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'; .\build-editor.ps1
    .\build-editor.ps1 -SkipBlueprintFormatterSync   # offline / local formatter tree
    .\build-editor.ps1 -HeadedScenarioSmoke -SkipBlueprintFormatterSync
      # headed UnrealEditor: RunCatalogMatrix + two UnrealAi.RunAgentTurn console scenarios (see scripts\run-headed-scenario-smoke.ps1)
    # Live headed qualitative suite (real API, manifest-driven): scripts\run-headed-live-scenarios.ps1 — docs\LIVE_HARNESS.md
    # Context manager multi-turn workflows: scripts\run-headed-context-workflows.ps1 — docs\CONTEXT_HARNESS.md
    # Full harness + iteration context for agents: docs\AGENT_HARNESS_HANDOFF.md

  Each build syncs Plugins\UnrealBlueprintFormatter from git (clone or pull --ff-only) unless skipped.

  If execution is blocked:
    powershell -ExecutionPolicy Bypass -File .\build-editor.ps1

  If linking fails with LNK1104 on UnrealEditor-UnrealAiEditor.dll, close Unreal Editor (it loads the plugin)
  and run again with -Restart, or close manually. -NoHotReloadFromIDE avoids the Live Coding mutex but cannot
  override a loaded DLL lock.

  -Restart: force-stop UnrealEditor / UnrealEditor-Cmd before compiling (avoids plugin DLL locks). Omit to
  leave a running editor untouched (incremental / Live Coding workflows).
#>
[CmdletBinding()]
param(
    [switch]$GenerateProjectFiles,
    [string]$EngineRoot = $(if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }),
    [switch]$Headless,
    [switch]$Restart,
    [switch]$AutomationTests,
    [switch]$HeadedScenarioSmoke,
    [switch]$SkipBlueprintFormatterSync,
    [string]$BlueprintFormatterRepoUrl = 'https://github.com/ChristianWebb0209/unreal-engine-blueprint-plugin.git',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArguments
)

foreach ($r in $RemainingArguments) {
    if ($r -eq '--headless' -or $r -eq '-headless') {
        $Headless = $true
    }
    if ($r -eq '--AutomationTests' -or $r -eq '-AutomationTests') {
        $AutomationTests = $true
    }
    if ($r -eq '--restart' -or $r -eq '-restart') {
        $Restart = $true
    }
    if ($r -eq '--SkipBlueprintFormatterSync' -or $r -eq '-SkipBlueprintFormatterSync') {
        $SkipBlueprintFormatterSync = $true
    }
    if ($r -eq '--HeadedScenarioSmoke' -or $r -eq '-HeadedScenarioSmoke') {
        $HeadedScenarioSmoke = $true
    }
}

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'blank.uproject'

function Sync-UnrealBlueprintFormatterPlugin {
    param(
        [string]$RepoRoot,
        [string]$RepoUrl,
        [switch]$Skip
    )
    if ($Skip) {
        Write-Host 'Skipping UnrealBlueprintFormatter git sync (-SkipBlueprintFormatterSync).' -ForegroundColor Yellow
        return
    }
    if ($env:UE_SKIP_BLUEPRINT_FORMATTER_SYNC -eq '1' -or $env:UE_SKIP_BLUEPRINT_FORMATTER_SYNC -eq 'true') {
        Write-Host 'Skipping UnrealBlueprintFormatter git sync (UE_SKIP_BLUEPRINT_FORMATTER_SYNC).' -ForegroundColor Yellow
        return
    }
    $gitExe = Get-Command git -ErrorAction SilentlyContinue
    if (-not $gitExe) {
        Write-Error "git is not on PATH; cannot sync Plugins\UnrealBlueprintFormatter. Install Git or use -SkipBlueprintFormatterSync."
    }
    $Dest = Join-Path $RepoRoot 'Plugins\UnrealBlueprintFormatter'
    if (Test-Path (Join-Path $Dest '.git')) {
        Write-Host 'Updating UnrealBlueprintFormatter (git pull --ff-only)...' -ForegroundColor Cyan
        Push-Location $Dest
        try {
            & git pull --ff-only
        } finally {
            Pop-Location
        }
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
        return
    }
    if (Test-Path $Dest) {
        Write-Error "Plugins\UnrealBlueprintFormatter exists but is not a git clone (missing .git). Remove or rename that folder, then run build again so the repository can be cloned."
    }
    Write-Host "Cloning UnrealBlueprintFormatter into Plugins (first run)..." -ForegroundColor Cyan
    & git clone $RepoUrl $Dest
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Sync-UnrealBlueprintFormatterPlugin -RepoRoot $ProjectRoot -RepoUrl $BlueprintFormatterRepoUrl -Skip:$SkipBlueprintFormatterSync
$BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$EditorCmdExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

if (-not (Test-Path $UProject)) {
    Write-Error "Project file not found: $UProject"
}
if (-not (Test-Path $BuildBat)) {
    Write-Error "Unreal Build.bat not found: $BuildBat`nSet UE_ENGINE_ROOT or pass -EngineRoot."
}

if ($GenerateProjectFiles) {
    Write-Host "Generating Visual Studio project files..." -ForegroundColor Cyan
    & $BuildBat -projectfiles "-project=$UProject" -game -rocket -progress
    exit $LASTEXITCODE
}

if ($Restart) {
    $unrealNames = @('UnrealEditor', 'UnrealEditor-Cmd')
    $stopped = $false
    foreach ($procName in $unrealNames) {
        $running = @(Get-Process -Name $procName -ErrorAction SilentlyContinue)
        if ($running.Count -gt 0) {
            if (-not $stopped) {
                Write-Host "-Restart: closing Unreal Editor process(es) before build..." -ForegroundColor Yellow
                $stopped = $true
            }
            $running | Stop-Process -Force -ErrorAction SilentlyContinue
        }
    }
    if ($stopped) {
        Start-Sleep -Seconds 2
        Write-Host "Editor processes terminated. Continuing build." -ForegroundColor Green
    }
}

Write-Host "Building BlankEditor (Development | Win64)..." -ForegroundColor Cyan
& $BuildBat BlankEditor Win64 Development "-project=$UProject" -waitmutex -NoHotReloadFromIDE
$BuildExit = $LASTEXITCODE
if ($BuildExit -ne 0) {
    exit $BuildExit
}

if ($HeadedScenarioSmoke) {
    $SmokeScript = Join-Path $ProjectRoot 'scripts\run-headed-scenario-smoke.ps1'
    if (-not (Test-Path $SmokeScript)) {
        Write-Error "Missing headed scenario script: $SmokeScript"
    }
    Write-Host 'Running headed console scenarios (UnrealEditor.exe, real RHI)...' -ForegroundColor Cyan
    & $SmokeScript -EngineRoot $EngineRoot
    exit $LASTEXITCODE
}

if ($AutomationTests) {
    if (-not (Test-Path $EditorCmdExe)) {
        Write-Error "UnrealEditor-Cmd.exe not found: $EditorCmdExe`nSet UE_ENGINE_ROOT or pass -EngineRoot."
    }
    Write-Host "Running UnrealAiEditor.Tools automation (headless)..." -ForegroundColor Cyan
    # UnrealEditor-Cmd must receive `-ExecCmds=Automation RunTests …;Quit` as a *single* argv token.
    # PowerShell's `-ExecCmds="…"` form can split the value; use a splatted argument list instead.
    $AutomationCmdArgs = @(
        "`"$UProject`"",
        '-unattended',
        '-nop4',
        '-NoSplash',
        '-nullrhi',
        '-nosound',
        '-ExecCmds=Automation RunTests UnrealAiEditor.Tools;Quit',
        '-log'
    )
    & $EditorCmdExe @AutomationCmdArgs
    $TestExit = $LASTEXITCODE
    if ($TestExit -ne 0) {
        Write-Error "Automation tests failed (exit $TestExit)."
        exit $TestExit
    }
    Write-Host "Automation tests passed." -ForegroundColor Green
}

if (-not $Headless) {
    if (-not (Test-Path $EditorExe)) {
        Write-Error "UnrealEditor.exe not found: $EditorExe`nSet UE_ENGINE_ROOT or pass -EngineRoot."
    }
    Write-Host "Launching Unreal Editor..." -ForegroundColor Cyan
    Start-Process -FilePath $EditorExe -ArgumentList "`"$UProject`"" -WorkingDirectory $ProjectRoot
}

exit 0
