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
    Set UE_ENGINE_ROOT in repo .env (see .env.example) or: $env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'; .\build-editor.ps1
    .\build-editor.ps1 -SkipBlueprintFormatterSync   # offline / local formatter tree
    # Batch headed suites + full logging: tests\long-running-tests\run-long-running-headed.ps1 — docs\tooling\AGENT_HARNESS_HANDOFF.md
    # Full harness + iteration context for agents: docs\tooling\AGENT_HARNESS_HANDOFF.md

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
    [string]$EngineRoot = '',
    [switch]$Headless,
    [switch]$Restart,
    [switch]$AutomationTests,
    [switch]$SkipBlueprintFormatterSync,
    [string]$BlueprintFormatterRepoUrl = 'https://github.com/ChristianWebb0209/ue-blueprint-formatter.git',
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
}

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'scripts\Import-RepoDotenv.ps1')
Import-RepoDotenv -RepoRoot $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }
}

$ProjectRoot = $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'blank.uproject'

if (-not ('UnrealWindowFocus' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class UnrealWindowFocus {
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@
}

function Start-ProcessWithoutStealingFocus {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    $previousForeground = [UnrealWindowFocus]::GetForegroundWindow()
    $proc = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -WorkingDirectory $WorkingDirectory -PassThru

    # Give the editor a moment to create a main window, then show it without activation.
    for ($i = 0; $i -lt 20 -and $proc.MainWindowHandle -eq 0; $i++) {
        Start-Sleep -Milliseconds 100
        try {
            $null = $proc.Refresh()
        } catch {
            break
        }
    }
    if ($proc.MainWindowHandle -ne 0) {
        # SW_SHOWMINNOACTIVE = 7 (minimized, without stealing focus)
        [void][UnrealWindowFocus]::ShowWindowAsync($proc.MainWindowHandle, 7)
    }
    if ($previousForeground -ne [IntPtr]::Zero) {
        [void][UnrealWindowFocus]::SetForegroundWindow($previousForeground)
    }
}

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
    $gitExe = Get-Command git -ErrorAction SilentlyContinue
    if (-not $gitExe) {
        Write-Error "git is not on PATH; cannot sync Plugins\UnrealBlueprintFormatter. Install Git or use -SkipBlueprintFormatterSync."
    }
    $Dest = Join-Path $RepoRoot 'Plugins\UnrealBlueprintFormatter'
    if (Test-Path (Join-Path $Dest '.git')) {
        # Keep existing clones healthy if the upstream repository was renamed/moved.
        Push-Location $Dest
        try {
            $currentOrigin = (& git remote get-url origin 2>$null)
            if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($currentOrigin) -and $currentOrigin -ne $RepoUrl) {
                Write-Host "Updating UnrealBlueprintFormatter origin URL -> $RepoUrl" -ForegroundColor Cyan
                & git remote set-url origin $RepoUrl
            }
        } finally {
            Pop-Location
        }
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
    Write-Host "Launching Unreal Editor without stealing focus..." -ForegroundColor Cyan
    Start-ProcessWithoutStealingFocus -FilePath $EditorExe -ArgumentList "`"$UProject`"" -WorkingDirectory $ProjectRoot
}

exit 0
