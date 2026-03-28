#requires -Version 5.1
<#
  Launch Unreal Editor (headed — real RHI window) and drive console scenarios:
    UnrealAi.RunCatalogMatrix <filter>
    UnrealAi.RunAgentTurn <msg>  (twice; real LLM via Project Settings API keys)
  Then exit. Runs tests/assert_harness_run.py on the latest harness run.jsonl (loose structural check).

  Agent handoff (prompts, catalog, escalation): docs\AGENT_HARNESS_HANDOFF.md

  From repo root:
    .\scripts\run-headed-scenario-smoke.ps1
    .\scripts\run-headed-scenario-smoke.ps1 -MatrixFilter "blueprint" -SkipCatalogMatrix
    $env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'; .\scripts\run-headed-scenario-smoke.ps1
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = '',
    [string]$MatrixFilter = 'blueprint',
    [switch]$SkipCatalogMatrix,
    [switch]$TakeoverLock,
    [int]$MaxLlmRounds = 2,
    [int]$WaitRetrievalReadySec = 0,
    [int]$EditorExitTimeoutSec = 0
)

$ErrorActionPreference = 'Stop'
$Script:SpawnedEditorPids = @()
$currentPid = $PID

$RepoRootForEnv = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Import-RepoDotenv.ps1')
Import-RepoDotenv -RepoRoot $RepoRootForEnv
. (Join-Path $PSScriptRoot 'Set-UnrealAiHeadedToolPackDefaults.ps1')

if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }
}

if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_HARNESS_SYNC_WAIT_MS)) {
    $env:UNREAL_AI_HARNESS_SYNC_WAIT_MS = '180000'
}
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_HTTP_REQUEST_TIMEOUT_SEC)) {
    $env:UNREAL_AI_HTTP_REQUEST_TIMEOUT_SEC = '30'
}
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_LLM_STREAM)) {
    $env:UNREAL_AI_LLM_STREAM = '0'
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $ProjectRoot 'blank.uproject'))) {
    $Cursor = $PSScriptRoot
    while ($Cursor -and -not (Test-Path (Join-Path $Cursor 'blank.uproject'))) {
        $Cursor = Split-Path -Parent $Cursor
    }
    $ProjectRoot = $Cursor
}
if (-not $ProjectRoot -or -not (Test-Path (Join-Path $ProjectRoot 'blank.uproject'))) {
    Write-Error "Could not locate blank.uproject (expected repo root parent of scripts/)."
}

$UProject = Join-Path $ProjectRoot 'blank.uproject'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $EditorExe)) {
    Write-Error "UnrealEditor.exe not found: $EditorExe"
}

New-Item -ItemType Directory -Force -Path (Join-Path $ProjectRoot 'Saved\UnrealAiEditor') | Out-Null
$lockPath = Join-Path $ProjectRoot 'Saved\UnrealAiEditor\scenario_smoke.lock.json'

$MsgA = Join-Path $ProjectRoot 'tests\harness_scenarios\user_scenario_a_selection.txt'
$MsgB = Join-Path $ProjectRoot 'tests\harness_scenarios\user_scenario_b_scene_search.txt'

if ($env:UNREAL_AI_LLM_FIXTURE) {
    Write-Warning "UNREAL_AI_LLM_FIXTURE is no longer supported (removed); ignoring value: $($env:UNREAL_AI_LLM_FIXTURE)"
    Remove-Item Env:\UNREAL_AI_LLM_FIXTURE -ErrorAction SilentlyContinue
}
Write-Host 'Headed smoke uses live LLM: configure API keys in Project Settings > Plugins > Unreal AI Editor. Usage may incur cost.' -ForegroundColor Yellow

if (-not (Test-Path -LiteralPath $MsgA)) { Write-Error "Scenario message file not found: $MsgA" }
if (-not (Test-Path -LiteralPath $MsgB)) { Write-Error "Scenario message file not found: $MsgB" }

function Escape-Win32QuotedArgContent {
    param([string]$Text)
    if ($null -eq $Text) { return '' }
    return $Text.Replace('"', '""')
}
Write-Host ''
Write-Host 'NOTE: The Editor window may look idle for many minutes during each RunAgentTurn.' -ForegroundColor Yellow
Write-Host '      That is normal: the game thread waits on the real LLM + tools. Watch:' -ForegroundColor Yellow
Write-Host "        - Window | Developer Tools | Output Log" -ForegroundColor DarkYellow
Write-Host "        - Or tail: Saved\UnrealAiEditor\HarnessRuns\<latest>\run.jsonl" -ForegroundColor DarkYellow
Write-Host ''

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $EditorExe
$psi.WorkingDirectory = $ProjectRoot
$psi.UseShellExecute = $false

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
    } catch {}
    if ($PreviousForeground -ne [IntPtr]::Zero) {
        try { [void][UnrealWindowFocus]::SetForegroundWindow($PreviousForeground) } catch {}
    }
}

function Acquire-RunLock {
    param([string]$Path, [switch]$Takeover)
    if (Test-Path -LiteralPath $Path) {
        $raw = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
        $existing = $null
        try { $existing = $raw | ConvertFrom-Json } catch {}
        if ($existing -and $existing.pid) {
            $proc = Get-Process -Id ([int]$existing.pid) -ErrorAction SilentlyContinue
            if ($proc -and -not $Takeover) {
                throw "Another scenario smoke runner is active (pid=$($existing.pid), started=$($existing.started_utc))."
            }
        }
    }
    $payload = [ordered]@{
        pid = $currentPid
        started_utc = (Get-Date).ToUniversalTime().ToString('o')
        host = $env:COMPUTERNAME
        script = $MyInvocation.MyCommand.Path
    }
    ($payload | ConvertTo-Json -Depth 4) + "`n" | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Release-RunLock {
    param([string]$Path)
    if (Test-Path -LiteralPath $Path) {
        $raw = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
        $existing = $null
        try { $existing = $raw | ConvertFrom-Json } catch {}
        if ($existing -and [int]$existing.pid -eq $currentPid) {
            Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
        }
    }
}

function Stop-SpawnedEditors {
    foreach ($pid in @($Script:SpawnedEditorPids)) {
        try {
            $proc = Get-Process -Id $pid -ErrorAction SilentlyContinue
            if ($proc) {
                $proc | Stop-Process -Force -ErrorAction SilentlyContinue
            }
        } catch {}
    }
}

function Stop-AllUnrealEditors {
    $procs = Get-Process UnrealEditor -ErrorAction SilentlyContinue
    if ($procs) {
        Write-Warning ("Detected {0} pre-existing UnrealEditor process(es); terminating for deterministic harness runs." -f $procs.Count)
        $procs | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }
}

function Invoke-HeadedExecCmds {
    param(
        [string]$ExecCmds,
        [string]$ExpectedRunJsonlPath = ''
    )
    $upEsc = Escape-Win32QuotedArgContent $UProject
    $ecEsc = Escape-Win32QuotedArgContent $ExecCmds
    $psi.Arguments = '"' + $upEsc + '" -unattended -nop4 -NoSplash -ExecCmds="' + $ecEsc + '" -log'
    $prevForeground = [UnrealWindowFocus]::GetForegroundWindow()
    $p = [System.Diagnostics.Process]::Start($psi)
    if ($p) { $Script:SpawnedEditorPids += $p.Id }
    if ($p) {
        Minimize-ProcessMainWindowNoActivate -Proc $p -PreviousForeground $prevForeground
    }
    function Test-RunJsonlFinished {
        param([string]$RunJsonlPath)
        if ([string]::IsNullOrWhiteSpace($RunJsonlPath)) { return $false }
        if (-not (Test-Path -LiteralPath $RunJsonlPath)) { return $false }
        try {
            $raw = Get-Content -LiteralPath $RunJsonlPath -Raw -Encoding UTF8 -ErrorAction Stop
            return $raw -match '"type"\s*:\s*"run_finished"'
        } catch {
            return $false
        }
    }

    $useWatchdog = ($EditorExitTimeoutSec -gt 0)
    $deadlineUtc = if ($useWatchdog) { (Get-Date).ToUniversalTime().AddSeconds([Math]::Max(1, $EditorExitTimeoutSec)) } else { $null }
    $sawTerminalRun = $false
    if (-not [string]::IsNullOrWhiteSpace($ExpectedRunJsonlPath)) {
        while ($true) {
            if ($p.HasExited) { break }
            if (Test-RunJsonlFinished -RunJsonlPath $ExpectedRunJsonlPath) {
                $sawTerminalRun = $true
                break
            }
            if ($useWatchdog -and ((Get-Date).ToUniversalTime() -ge $deadlineUtc)) {
                break
            }
            Start-Sleep -Milliseconds 200
        }
        if ($sawTerminalRun -and -not $p.HasExited) {
            try { [void]$p.CloseMainWindow() } catch {}
            if (-not $p.WaitForExit(3000)) {
                try { $p.Kill() } catch {}
                try { $p.WaitForExit() } catch {}
            }
            Write-Host "UnrealEditor exit code: $($p.ExitCode)" -ForegroundColor $(if ($p.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
            return $p.ExitCode
        }
    }
    if (-not $p.HasExited) {
        if ($useWatchdog) {
            Write-Warning "UnrealEditor did not reach terminal state within $EditorExitTimeoutSec seconds; force-killing to keep harness runs moving."
            try { $p.Kill() } catch {}
            try { $p.WaitForExit() } catch {}
        } else {
            [void]$p.WaitForExit()
        }
    }
    Write-Host "UnrealEditor exit code: $($p.ExitCode)" -ForegroundColor $(if ($p.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
    return $p.ExitCode
}

Acquire-RunLock -Path $lockPath -Takeover:$TakeoverLock
trap {
    Remove-Item Env:\UNREAL_AI_HARNESS_MESSAGE_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_OUTPUT_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_MAX_LLM_ROUNDS -ErrorAction SilentlyContinue
    Stop-SpawnedEditors
    Release-RunLock -Path $lockPath
    throw
}

Stop-AllUnrealEditors

if (-not $SkipCatalogMatrix) {
    if ([string]::IsNullOrWhiteSpace($MatrixFilter)) {
        $matrixCmd = 'UnrealAi.RunCatalogMatrix'
    } else {
        $matrixCmd = "UnrealAi.RunCatalogMatrix $MatrixFilter"
    }
    $matrixExec = "$matrixCmd,QUIT_EDITOR"
    Write-Host "Headed scenario smoke (matrix): $matrixExec" -ForegroundColor Cyan
    [void](Invoke-HeadedExecCmds $matrixExec)
}

foreach ($msg in @($MsgA, $MsgB)) {
    $env:UNREAL_AI_HARNESS_MESSAGE_FILE = (Resolve-Path -LiteralPath $msg).Path
    $turnStamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd_HHmmss_fff')
    $turnLabel = if ($msg -eq $MsgA) { 'turn_a' } else { 'turn_b' }
    $turnOutDir = Join-Path $ProjectRoot ("Saved\UnrealAiEditor\HarnessRuns\smoke_{0}_{1}" -f $turnLabel, $turnStamp)
    New-Item -ItemType Directory -Force -Path $turnOutDir | Out-Null
    $env:UNREAL_AI_HARNESS_OUTPUT_DIR = $turnOutDir
    if ($MaxLlmRounds -gt 0) {
        $env:UNREAL_AI_HARNESS_MAX_LLM_ROUNDS = [string]$MaxLlmRounds
    }
    try {
        $turnExec = 'UnrealAi.RunAgentTurn'
        if ($WaitRetrievalReadySec -gt 0) {
            $turnExec = "$turnExec,UnrealAi.Retrieval.WaitForReady $WaitRetrievalReadySec"
        }
        $turnExec = "$turnExec,QUIT_EDITOR"
        Write-Host "Headed scenario smoke (turn): UNREAL_AI_HARNESS_MESSAGE_FILE=$($env:UNREAL_AI_HARNESS_MESSAGE_FILE) | $turnExec" -ForegroundColor Cyan
        [void](Invoke-HeadedExecCmds $turnExec (Join-Path $turnOutDir 'run.jsonl'))
    }
    finally {
        Remove-Item Env:\UNREAL_AI_HARNESS_MESSAGE_FILE -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_OUTPUT_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_MAX_LLM_ROUNDS -ErrorAction SilentlyContinue
    }
}

$runsRoot = Join-Path $ProjectRoot 'Saved\UnrealAiEditor\HarnessRuns'
if (-not (Test-Path $runsRoot)) {
    Write-Warning "No HarnessRuns folder at $runsRoot"
    exit 1
}
$runs = Get-ChildItem -Path $runsRoot -Directory | Sort-Object Name -Descending
if (-not $runs -or $runs.Count -lt 1) {
    Write-Warning 'No harness run directories found.'
    exit 1
}

$latest = $runs | Select-Object -First 1
$jsonl = Join-Path $latest.FullName 'run.jsonl'
if (-not (Test-Path $jsonl)) {
    Write-Warning "No run.jsonl in $($latest.FullName)"
    exit 1
}
Write-Host "Latest harness artifact (loose assert): $jsonl" -ForegroundColor Cyan
Write-Host 'Tool order varies with live API; review run.jsonl for qualitative behavior.' -ForegroundColor DarkGray
& python (Join-Path $ProjectRoot 'tests\assert_harness_run.py') $jsonl
if ($LASTEXITCODE -eq 1) {
    exit 1
}
Write-Host 'Headed scenario smoke finished.' -ForegroundColor Green
Stop-SpawnedEditors
Release-RunLock -Path $lockPath
exit 0
