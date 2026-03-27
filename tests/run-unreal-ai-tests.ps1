#requires -Version 5.1
<#
  Run Unreal Editor automation tests for UnrealAiEditor (blank.uproject), capture logs + tool matrix JSON.

  Agent handoff (prompts, harness, escalation): docs\AGENT_HARNESS_HANDOFF.md

  Usage (from repo root):
    .\tests\run-unreal-ai-tests.ps1
    .\tests\run-unreal-ai-tests.ps1 -Build
    .\tests\run-unreal-ai-tests.ps1 -Headed
    .\tests\run-unreal-ai-tests.ps1 -TestFilter UnrealAiEditor -MatrixFilter blueprint
    .\tests\run-unreal-ai-tests.ps1 -Summarize
    $env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'; .\tests\run-unreal-ai-tests.ps1

  -Headed: use UnrealEditor.exe (visible window) instead of UnrealEditor-Cmd.exe. Same ExecCmds automation.

  -AllowDialogs: omit -unattended so blocking editor dialogs can appear (automation may hang if a dialog waits).
                 Default keeps -unattended for non-interactive runs.

  UE 5.7: uses -ExecCmds="Automation RunTests <filter>; Quit" (validated pattern for Session Frontend tests).

  Close Unreal Editor before -Build if UnrealAiEditor.dll is locked.

  Outputs (for LLM iteration):
    tests\out\editor-last.log   - newest Saved\Logs\*.log after the run
    tests\out\last-matrix.json - copy of Saved\UnrealAiEditor\Automation\tool_matrix_last.json (if produced)

  In-editor (no relaunch): open Output Log and run `UnrealAi.RunCatalogMatrix` (optional filter substring).

  NOT STUCK: The editor prints almost nothing to this console until it exits. Cold start + automation can take
  many minutes. Lines like "google_update_settings" / Chromium are harmless. Use -CatalogMatrixOnly for a
  faster matrix-only run, or watch Saved\Logs\*.log while waiting.

  -CatalogMatrixOnly: same as -TestFilter UnrealAiEditor.Tools.CatalogMatrix (skips harness/unit tests; still
                     invokes every catalog tool in the matrix - can be 10-30+ minutes).
#>
[CmdletBinding()]
param(
    [switch]$Build,
    [string]$EngineRoot = '',
    [string]$TestFilter = 'UnrealAiEditor',
    [switch]$CatalogMatrixOnly,
    [string]$MatrixFilter = '',
    [switch]$NullRhi,
    [switch]$Headed,
    [switch]$AllowDialogs,
    [switch]$Summarize,
    [string[]]$ExtraEditorArgs = @()
)

$ErrorActionPreference = 'Stop'

. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\Import-RepoDotenv.ps1')
Import-RepoDotenv -RepoRoot (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }
}

if ($CatalogMatrixOnly) {
    $TestFilter = 'UnrealAiEditor.Tools.CatalogMatrix'
}
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'blank.uproject'
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$TestsOutDir = Join-Path $ProjectRoot 'tests\out'
$MatrixSaved = Join-Path $ProjectRoot 'Saved\UnrealAiEditor\Automation\tool_matrix_last.json'
$MatrixOut = Join-Path $TestsOutDir 'last-matrix.json'
$LogOut = Join-Path $TestsOutDir 'editor-last.log'
$SummarizePy = Join-Path $ProjectRoot 'tests\summarize_tool_matrix.py'

if (-not (Test-Path $UProject)) {
    Write-Error ('Project file not found: ' + $UProject)
}

if (-not (Test-Path $TestsOutDir)) {
    New-Item -ItemType Directory -Path $TestsOutDir -Force | Out-Null
}

if ($Build) {
    $BuildScript = Join-Path $ProjectRoot 'build-editor.ps1'
    Write-Host "Building BlankEditor (headless)..." -ForegroundColor Cyan
    & $BuildScript -Headless -EngineRoot $EngineRoot
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$EditorBinary = if ($Headed) { $EditorExe } else { $EditorCmd }
if (-not (Test-Path $EditorBinary)) {
    $exeName = if ($Headed) { 'UnrealEditor.exe' } else { 'UnrealEditor-Cmd.exe' }
    Write-Error ($exeName + ' not found: ' + $EditorBinary + "`nSet UE_ENGINE_ROOT or pass -EngineRoot.")
}

try {
    $env:GIT_COMMIT = (git -C $ProjectRoot rev-parse HEAD 2>$null)
} catch {
    $env:GIT_COMMIT = ''
}

$ExecCmdsValue = "Automation RunTests $TestFilter;Quit"
$CmdArgs = @(
    "`"$UProject`"",
    '-nop4',
    '-nosplash',
    '-log',
    '-LogCmds=LogAutomationController Verbose, LogAutomationCommandLine Verbose, LogAutomationTest Verbose'
)
if (-not $AllowDialogs) {
    $CmdArgs += '-unattended'
}
if ($NullRhi) {
    $CmdArgs += '-nullrhi'
}
if ($MatrixFilter) {
    $CmdArgs += "-UnrealAiToolMatrixFilter=$MatrixFilter"
}
foreach ($x in $ExtraEditorArgs) {
    if (-not [string]::IsNullOrWhiteSpace($x)) {
        $CmdArgs += $x
    }
}
# UnrealEditor must receive -ExecCmds value as one argv token.
# Quote only the value side to avoid silent command truncation.
$CmdArgs += "-ExecCmds=`"$ExecCmdsValue`""

$logsDir = Join-Path $ProjectRoot 'Saved\Logs'

# UnrealEditor.exe (headed) is a GUI app: it will not write engine log lines to this PowerShell console.
# UnrealEditor-Cmd streams more to the attached console. We always tail Saved\Logs\*.log here so headed runs stay readable.
$script:TailedLogPath = ''
$script:TailedLogByteOffset = 0L

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

function Minimize-ProcessMainWindowNoActivate {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Proc,
        [Parameter(Mandatory = $true)][IntPtr]$PreviousForeground
    )
    try {
        for ($i = 0; $i -lt 50 -and $Proc.MainWindowHandle -eq 0; $i++) {
            Start-Sleep -Milliseconds 100
            $null = $Proc.Refresh()
        }
        if ($Proc.MainWindowHandle -ne 0) {
            # SW_SHOWMINNOACTIVE = 7 (minimized, does not activate)
            [void][UnrealWindowFocus]::ShowWindowAsync($Proc.MainWindowHandle, 7)
        }
    } catch {
        # Best-effort: ignore focus/minimize failures
    }
    if ($PreviousForeground -ne [IntPtr]::Zero) {
        try { [void][UnrealWindowFocus]::SetForegroundWindow($PreviousForeground) } catch {}
    }
}

function Get-NewestSavedEditorLogPath {
    param([string]$Dir)
    if (-not (Test-Path -LiteralPath $Dir)) {
        return $null
    }
    $item = Get-ChildItem -Path $Dir -Filter '*.log' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($item) {
        return $item.FullName
    }
    return $null
}

function Write-NewBytesFromEditorLog {
    $path = Get-NewestSavedEditorLogPath -Dir $logsDir
    if (-not $path) {
        return
    }
    if ($path -ne $script:TailedLogPath) {
        if ($script:TailedLogPath) {
            Write-Host ("[run-unreal-ai-tests] Log file rotated -> " + (Split-Path -Leaf $path)) -ForegroundColor DarkCyan
        } else {
            Write-Host ("[run-unreal-ai-tests] Tailing log: " + $path) -ForegroundColor DarkCyan
        }
        $script:TailedLogPath = $path
        $script:TailedLogByteOffset = 0L
    }
    try {
        $len = (Get-Item -LiteralPath $path).Length
        if ($len -lt $script:TailedLogByteOffset) {
            $script:TailedLogByteOffset = 0L
        }
        if ($len -le $script:TailedLogByteOffset) {
            return
        }
        $fs = [System.IO.File]::Open(
            $path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
        try {
            $null = $fs.Seek($script:TailedLogByteOffset, [System.IO.SeekOrigin]::Begin)
            $buf = New-Object byte[] ($len - $script:TailedLogByteOffset)
            $read = $fs.Read($buf, 0, $buf.Length)
            if ($read -gt 0) {
                $script:TailedLogByteOffset = $len
                $text = [System.Text.Encoding]::UTF8.GetString($buf, 0, $read)
                # Normalize and split lines; avoid dumping huge blobs as one line
                $text = $text -replace "`r`n", "`n" -replace "`r", "`n"
                foreach ($line in ($text -split "`n")) {
                    if ($line.Length -gt 0) {
                        Write-Host $line
                    }
                }
            }
        } finally {
            $fs.Dispose()
        }
    } catch {
        # Log may be briefly locked or mid-rotation; skip this tick
    }
}

Write-Host "Running: $EditorBinary" -ForegroundColor Cyan
if ($Headed) {
    Write-Host "  Mode: headed (visible editor window; engine log is tailed from Saved\Logs below)" -ForegroundColor DarkGray
}
Write-Host "  Args: $($CmdArgs -join ' ')" -ForegroundColor DarkGray
Write-Host ''
Write-Host "  Startup + tests can take 5-30+ minutes (CatalogMatrix invokes every catalog tool)." -ForegroundColor Yellow
Write-Host "  Log directory: $logsDir" -ForegroundColor DarkGray
Write-Host ''

$t0 = Get-Date
# Headed UnrealEditor.exe should not use -NoNewWindow: it can interfere with console ownership on some setups.
$procArgs = @{
    FilePath = $EditorBinary
    ArgumentList = $CmdArgs
    WorkingDirectory = $ProjectRoot
    PassThru = $true
}
$prevForeground = [UnrealWindowFocus]::GetForegroundWindow()
if (-not $Headed) {
    # Keep cmd runs from popping the active console to front.
    $procArgs['WindowStyle'] = 'Minimized'
}
$p = Start-Process @procArgs
if (-not $p) {
    Write-Error 'Failed to start Unreal Editor process.'
}
if ($Headed) {
    Minimize-ProcessMainWindowNoActivate -Proc $p -PreviousForeground $prevForeground
}
$nextHeartbeatSec = 30
while (-not $p.HasExited) {
    Start-Sleep -Seconds 2
    Write-NewBytesFromEditorLog
    $sec = [int]((Get-Date) - $t0).TotalSeconds
    if ($sec -ge $nextHeartbeatSec) {
        $nextHeartbeatSec = $sec + 30
        $min = [math]::Floor($sec / 60)
        $srem = $sec % 60
        Write-Host ('[run-unreal-ai-tests] Editor still running ({0} min {1} sec, PID {2})...' -f $min, $srem, $p.Id) -ForegroundColor DarkGray
        if ($sec -gt 7200) {
            Write-Warning 'Over 2 hours elapsed - if the editor is not actually running, kill the process and check Saved/Logs.'
        }
    }
}
# Drain any trailing log bytes after exit
Write-NewBytesFromEditorLog

$p.Refresh()
$procExit = 0
try {
    if ($null -ne $p.ExitCode) {
        $procExit = $p.ExitCode
    }
} catch {
    $procExit = 0
}

# Copy newest log in Saved\Logs (typically the editor session that just finished)
$failLogParse = $false
try {
    if (Test-Path $logsDir) {
        $picked = Get-ChildItem -Path $logsDir -Filter '*.log' -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($picked) {
            Copy-Item -LiteralPath $picked.FullName -Destination $LogOut -Force
        }
    }
} catch {
    $failLogParse = $true
    Write-Warning ('Could not copy editor log: ' + $_)
}

$failCountFromLog = 0
$logText = ''
if (Test-Path $LogOut) {
    $logText = Get-Content -LiteralPath $LogOut -Raw -ErrorAction SilentlyContinue
    if ($logText) {
        $failCountFromLog += ([regex]::Matches($logText, 'Result=Fail')).Count
        if ($logText -match 'Failed\s+Test\s+Count\s*:\s*(\d+)') {
            $failCountFromLog = [Math]::Max($failCountFromLog, [int]$Matches[1])
        }
    }
}

if (Test-Path $MatrixSaved) {
    Copy-Item -LiteralPath $MatrixSaved -Destination $MatrixOut -Force
}

# --- Summary (stdout): easy for LLMs to grep ---
Write-Host ''
Write-Host '--- Unreal AI test run summary ---' -ForegroundColor Cyan
Write-Host ('ProcessExitCode: ' + $procExit)
Write-Host ('Log:             ' + $LogOut)
Write-Host ('Matrix JSON:     ' + $MatrixOut)
Write-Host ('Result=Fail hits in log: ' + $failCountFromLog)
if ($failLogParse) {
    Write-Host 'Log copy warning: see stderr above' -ForegroundColor Yellow
}

$contractViolations = $null
if (Test-Path $MatrixOut) {
    try {
        $j = Get-Content -LiteralPath $MatrixOut -Raw | ConvertFrom-Json
        $contractViolations = $j.summary.contract_violations
        Write-Host ('Matrix summary.contract_violations: ' + $contractViolations)
    } catch {
        Write-Host 'Matrix JSON: could not parse summary (file still copied)' -ForegroundColor Yellow
    }
} else {
    Write-Host 'Matrix JSON: not found (CatalogMatrix test may not have run or failed before write)' -ForegroundColor Yellow
}

$summarizeExit = 0
if ($Summarize) {
    if (-not (Test-Path -LiteralPath $SummarizePy)) {
        Write-Warning ('Summarize script not found: ' + $SummarizePy)
    } elseif (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        Write-Warning 'python not on PATH; skipping -Summarize'
    } else {
        Write-Host ''
        Write-Host '--- summarize_tool_matrix stdout ---' -ForegroundColor Cyan
        & python $SummarizePy --matrix $MatrixOut
        $summarizeExit = $LASTEXITCODE
    }
}

$finalExit = 0
if ($procExit -ne 0) {
    $finalExit = $procExit
} elseif ($failCountFromLog -gt 0) {
    $finalExit = 1
} elseif ($null -ne $contractViolations -and [int]$contractViolations -gt 0) {
    $finalExit = 1
} elseif ($Summarize -and $summarizeExit -ne 0) {
    $finalExit = $summarizeExit
}

if ($finalExit -eq 0) {
    Write-Host ('FinalExitCode: ' + $finalExit) -ForegroundColor Green
} else {
    Write-Host ('FinalExitCode: ' + $finalExit) -ForegroundColor Red
}
exit $finalExit
