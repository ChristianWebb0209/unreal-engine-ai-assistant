#requires -Version 5.1
<#
  Live headed qualitative tier: launch headed Unreal Editor and run manifest-driven UnrealAi.RunAgentTurn
  (real API). Copies each harness output to tests/out/live_runs/<suite>/<scenario_id>/.

  Default: one editor launch per scenario (isolates failures). Use -SingleSession to chain all turns in one ExecCmds (faster; on failure later scenarios may not run).

  From repo root:
    .\scripts\run-headed-live-scenarios.ps1
    .\scripts\run-headed-live-scenarios.ps1 -Manifest tests\live_scenarios\manifest.json -MaxScenarios 3
    .\scripts\run-headed-live-scenarios.ps1 -DryRun
    $env:UNREAL_AI_HARNESS_DUMP_CONTEXT = '1'; .\scripts\run-headed-live-scenarios.ps1

  See docs\AGENT_HARNESS_HANDOFF.md.
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = '',
    [string]$ProjectRoot = '',
    [string]$Manifest = '',
    [int]$MaxScenarios = 0,
    [switch]$SkipMatrix,
    [string]$MatrixFilter = '',
    [switch]$SingleSession,
    [switch]$DumpContext,
    [switch]$DryRun,
    [switch]$KillPreExistingEditors,
    [switch]$TakeoverLock,
    [int]$MaxLlmRounds = 8,
    [int]$EditorExitTimeoutSec = 0,
    [int]$InfraRetries = 0
)

$ErrorActionPreference = 'Stop'
$Script:SpawnedEditorPids = @()

$RepoRootForEnv = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Import-RepoDotenv.ps1')
Import-RepoDotenv -RepoRoot $RepoRootForEnv
. (Join-Path $PSScriptRoot 'Set-UnrealAiHeadedToolPackDefaults.ps1')

if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }
}

if (-not $ProjectRoot) {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
    if (-not (Test-Path (Join-Path $ProjectRoot 'blank.uproject'))) {
        $Cursor = $PSScriptRoot
        while ($Cursor -and -not (Test-Path (Join-Path $Cursor 'blank.uproject'))) {
            $Cursor = Split-Path -Parent $Cursor
        }
        $ProjectRoot = $Cursor
    }
}
if (-not $ProjectRoot -or -not (Test-Path (Join-Path $ProjectRoot 'blank.uproject'))) {
    Write-Error "Could not locate blank.uproject (set -ProjectRoot)."
}

if (-not $Manifest) {
    $Manifest = Join-Path $ProjectRoot 'tests\live_scenarios\manifest.json'
}
if (-not (Test-Path $Manifest)) {
    Write-Error "Manifest not found: $Manifest"
}

$UProject = Join-Path $ProjectRoot 'blank.uproject'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $EditorExe)) {
    Write-Error "UnrealEditor.exe not found: $EditorExe (set UE_ENGINE_ROOT or -EngineRoot)"
}

if ($env:UNREAL_AI_LLM_FIXTURE) {
    Write-Warning "UNREAL_AI_LLM_FIXTURE is no longer supported; ignoring: $($env:UNREAL_AI_LLM_FIXTURE)"
    Remove-Item Env:\UNREAL_AI_LLM_FIXTURE -ErrorAction SilentlyContinue
}

$manifestDir = Split-Path -Parent $Manifest
$raw = Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8
$doc = $raw | ConvertFrom-Json
$suiteId = [string]$doc.suite_id
if ([string]::IsNullOrWhiteSpace($suiteId)) {
    $suiteId = 'default_suite'
}

$scenarios = @($doc.scenarios)
if ($MaxScenarios -gt 0 -and $scenarios.Count -gt $MaxScenarios) {
    $scenarios = $scenarios[0..($MaxScenarios - 1)]
}

$outRoot = Join-Path $ProjectRoot "tests\out\live_runs\$suiteId"
if (Test-Path -LiteralPath $outRoot) {
    Remove-Item -LiteralPath $outRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$lockDir = Join-Path $ProjectRoot 'Saved\UnrealAiEditor'
New-Item -ItemType Directory -Force -Path $lockDir | Out-Null
$lockPath = Join-Path $lockDir 'live_scenarios.lock.json'
$currentPid = $PID

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
                throw "Another live scenario runner is active (pid=$($existing.pid), started=$($existing.started_utc)). Re-run with -TakeoverLock to override."
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

Acquire-RunLock -Path $lockPath -Takeover:$TakeoverLock
trap {
    Remove-Item Env:\UNREAL_AI_HARNESS_MESSAGE_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_MAX_LLM_ROUNDS -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_OUTPUT_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_THREAD_GUID -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_AGENT_MODE -ErrorAction SilentlyContinue
    Stop-SpawnedEditors
    Release-RunLock -Path $lockPath
    throw
}

function Get-HarnessRunDirs {
    param([string]$RunsRoot)
    if (-not (Test-Path $RunsRoot)) { return @() }
    Get-ChildItem -Path $RunsRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending
}

function Copy-LatestHarnessRun {
    param(
        [string]$RunsRoot,
        [string]$DestDir
    )
    $latest = Get-HarnessRunDirs $RunsRoot | Select-Object -First 1
    if (-not $latest) { return $false }
    if (Test-Path -LiteralPath $DestDir) {
        Remove-Item -LiteralPath $DestDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
    Copy-Item -LiteralPath $latest.FullName -Destination $DestDir -Recurse -Force
    return $true
}

function Get-RunIntegrity {
    param([string]$RunJsonlPath)
    $result = [ordered]@{
        run_jsonl_exists     = $false
        run_started_present  = $false
        run_finished_present = $false
        run_id               = $null
        run_finished_success = $null
        row_count            = 0
        artifact_integrity   = 'missing'
    }
    if (-not (Test-Path -LiteralPath $RunJsonlPath)) {
        return $result
    }
    $result.run_jsonl_exists = $true
    $lines = Get-Content -LiteralPath $RunJsonlPath -Encoding UTF8 -ErrorAction SilentlyContinue
    if ($null -eq $lines) {
        $lines = @()
    }
    $result.row_count = $lines.Count
    foreach ($line in $lines) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $obj = $null
        try {
            $obj = $line | ConvertFrom-Json -ErrorAction Stop
        }
        catch {
            continue
        }
        if ($obj.type -eq 'run_started') {
            $result.run_started_present = $true
            if (-not [string]::IsNullOrWhiteSpace([string]$obj.run_id)) {
                $result.run_id = [string]$obj.run_id
            }
        }
        elseif ($obj.type -eq 'run_finished') {
            $result.run_finished_present = $true
            $result.run_finished_success = [bool]$obj.success
        }
    }
    if ($result.run_started_present -and $result.run_finished_present) {
        $result.artifact_integrity = 'ok'
    }
    elseif ($result.run_started_present -or $result.run_finished_present) {
        $result.artifact_integrity = 'partial'
    }
    else {
        $result.artifact_integrity = 'stale'
    }
    return $result
}

function Get-ContextDumpIntegrity {
    param(
        [string]$RunDir,
        [bool]$ExpectContextDumps
    )
    $files = @()
    if (Test-Path -LiteralPath $RunDir) {
        $files = Get-ChildItem -LiteralPath $RunDir -Filter 'context_window*.txt' -File -ErrorAction SilentlyContinue
    }
    $count = if ($files) { $files.Count } else { 0 }
    return [ordered]@{
        expected = $ExpectContextDumps
        emitted_count = $count
        ok = (-not $ExpectContextDumps) -or ($count -gt 0)
    }
}

function Get-LiveRunMetrics {
    param([string]$RunJsonlPath)

    $out = [ordered]@{
        tool_failure_top = @()
        normalization_applied_count = 0
        low_confidence_search_count = 0
        low_confidence_max_streak = 0
    }

    if ([string]::IsNullOrWhiteSpace($RunJsonlPath) -or -not (Test-Path -LiteralPath $RunJsonlPath)) {
        return $out
    }

    $callIdToArgsJson = @{}
    $failCounts = @{}
    $normCount = 0
    $lowCount = 0
    $lowPrevSig = $null
    $lowStreak = 0
    $lowMax = 0

    foreach ($line in (Get-Content -LiteralPath $RunJsonlPath -Encoding UTF8 -ErrorAction SilentlyContinue)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $obj = $null
        try { $obj = $line | ConvertFrom-Json } catch { continue }

        $t = [string]$obj.type
        if ($t -eq 'tool_start') {
            $callIdToArgsJson[[string]$obj.call_id] = [string]$obj.arguments_json
            continue
        }

        if ($t -ne 'tool_finish') { continue }

        $tool = [string]$obj.tool
        $callId = [string]$obj.call_id
        $success = [bool]$obj.success
        $resultPreview = [string]$obj.result_preview

        if (-not $success) {
            $argsJson = $null
            if ($callIdToArgsJson.ContainsKey($callId)) {
                $argsJson = [string]$callIdToArgsJson[$callId]
            }
            if ([string]::IsNullOrWhiteSpace($argsJson)) { $argsJson = '' }

            # Shape = tool + canonicalized arguments string prefix (small enough for stable grouping).
            $normArgs = ($argsJson -replace '\s+', ' ') -replace '^\s+|\s+$', ''
            if ($normArgs.Length -gt 160) { $normArgs = $normArgs.Substring(0, 160) }
            # Stable tool failure signature used for grouping; keep deterministic and short.
            $shapeKey = "$tool|$normArgs"

            if ($failCounts.ContainsKey($shapeKey)) {
                $failCounts[$shapeKey] = [int]$failCounts[$shapeKey] + 1
            } else {
                $failCounts[$shapeKey] = 1
            }
        }

        if (-not [string]::IsNullOrWhiteSpace($resultPreview) -and $resultPreview -match '\"normalization_applied\"\\s*:\\s*true') {
            $normCount++
        }

        if ($tool -eq 'asset_index_fuzzy_search' -or $tool -eq 'scene_fuzzy_search') {
            if (-not [string]::IsNullOrWhiteSpace($resultPreview) -and $resultPreview -match '\"low_confidence\"\\s*:\\s*true') {
                $lowCount++
                $argsJson2 = $null
                if ($callIdToArgsJson.ContainsKey($callId)) {
                    $argsJson2 = [string]$callIdToArgsJson[$callId]
                }
                if ([string]::IsNullOrWhiteSpace($argsJson2)) { $argsJson2 = '' }

                $q = $null
                if ($argsJson2 -match '\"query\"\\s*:\\s*\"([^\"]+)\"') { $q = $Matches[1] }
                elseif ($argsJson2 -match '\"search_string\"\\s*:\\s*\"([^\"]+)\"') { $q = $Matches[1] }
                elseif ($argsJson2 -match '\"name_prefix\"\\s*:\\s*\"([^\"]+)\"') { $q = $Matches[1] }
                elseif ($argsJson2 -match '\"filter\"\\s*:\\s*\"([^\"]+)\"') { $q = $Matches[1] }
                if ([string]::IsNullOrWhiteSpace($q)) { $q = ($argsJson2 -replace '\s+', ' ') }

                $sig = "$tool|$q"
                if ($lowPrevSig -eq $sig) {
                    $lowStreak++
                } else {
                    $lowPrevSig = $sig
                    $lowStreak = 1
                }
                if ($lowStreak -gt $lowMax) { $lowMax = $lowStreak }
            }
        }
    }

    $top = @()
    $failCounts.GetEnumerator() | Sort-Object -Property Value -Descending | Select-Object -First 5 | ForEach-Object {
        $top += [ordered]@{ tool_shape = $_.Key; count = [int]$_.Value }
    }

    $out.tool_failure_top = $top
    $out.normalization_applied_count = [int]$normCount
    $out.low_confidence_search_count = [int]$lowCount
    $out.low_confidence_max_streak = [int]$lowMax
    return $out
}

function Get-ThreadIdFromContextTrace {
    param([string]$RunDir)
    if (-not (Test-Path -LiteralPath $RunDir)) { return $null }
    $trace = Join-Path $RunDir 'context_build_trace_run_started.json'
    if (Test-Path -LiteralPath $trace) {
        try {
            $doc = (Get-Content -LiteralPath $trace -Raw -Encoding UTF8) | ConvertFrom-Json
            $tid = [string]$doc.thread_id
            if (-not [string]::IsNullOrWhiteSpace($tid)) {
                return $tid
            }
        }
        catch {}
    }
    $anyTrace = Get-ChildItem -LiteralPath $RunDir -Filter 'context_build_trace_*.json' -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($anyTrace) {
        try {
            $doc2 = (Get-Content -LiteralPath $anyTrace.FullName -Raw -Encoding UTF8) | ConvertFrom-Json
            $tid2 = [string]$doc2.thread_id
            if (-not [string]::IsNullOrWhiteSpace($tid2)) {
                return $tid2
            }
        }
        catch {}
    }
    return $null
}

function Copy-ContextDecisionLogsForThread {
    param(
        [string]$ProjectRootPath,
        [string]$ThreadId,
        [string]$DestRunDir
    )
    if ([string]::IsNullOrWhiteSpace($ThreadId)) { return 0 }
    $src = Join-Path $ProjectRootPath ("Saved\UnrealAiEditor\ContextDecisionLogs\{0}" -f $ThreadId)
    if (-not (Test-Path -LiteralPath $src)) { return 0 }
    $dest = Join-Path $DestRunDir 'context_decision_logs'
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $copied = 0
    $files = Get-ChildItem -LiteralPath $src -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '*.jsonl' -or $_.Name -like '*-summary.md' }
    foreach ($f in $files) {
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $dest $f.Name) -Force
        $copied++
    }
    return $copied
}

function Build-RunAgentTurnExec {
    param(
        [string]$MsgAbs,
        [string]$Mode
    )
    # Message path is passed via UNREAL_AI_HARNESS_MESSAGE_FILE (see UnrealAi.RunAgentTurn) so -ExecCmds
    # does not need file paths (UE splits ExecCmds on spaces; quoting paths inside -ExecCmds is brittle).
    $m = if ($Mode) { $Mode.ToLowerInvariant() } else { 'agent' }
    Remove-Item Env:\UNREAL_AI_HARNESS_THREAD_GUID -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_AGENT_MODE -ErrorAction SilentlyContinue
    if ($m -ne 'agent') {
        $env:UNREAL_AI_HARNESS_THREAD_GUID = [guid]::NewGuid().ToString()
        $env:UNREAL_AI_HARNESS_AGENT_MODE = $m
    }
    return 'UnrealAi.RunAgentTurn'
}

if ($DryRun) {
    Write-Host "Dry run: suite=$suiteId scenarios=$($scenarios.Count)" -ForegroundColor Cyan
    $i = 0
    foreach ($s in $scenarios) {
        $i++
        $mf = Join-Path $manifestDir ([string]$s.message_file)
        Write-Host ("  [{0}] {1} -> {2}" -f $i, $s.id, $mf)
    }
    Write-Host "Output root would be: $outRoot"
    exit 0
}

$runsRoot = Join-Path $ProjectRoot 'Saved\UnrealAiEditor\HarnessRuns'

# Child editor inherits env. Defaults avoid sync_wait_timeout before HTTP completes (see UnrealAiHarnessScenarioRunner).
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_HARNESS_SYNC_WAIT_MS)) {
    $env:UNREAL_AI_HARNESS_SYNC_WAIT_MS = '180000'
}
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_HTTP_REQUEST_TIMEOUT_SEC)) {
    $env:UNREAL_AI_HTTP_REQUEST_TIMEOUT_SEC = '30'
}
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_LLM_STREAM)) {
    $env:UNREAL_AI_LLM_STREAM = '0'
}
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_CONTEXT_DECISION_LOG)) {
    $env:UNREAL_AI_CONTEXT_DECISION_LOG = '1'
}

function Escape-Win32QuotedArgContent {
    param([string]$Text)
    if ($null -eq $Text) { return '' }
    return $Text.Replace('"', '""')
}

function Invoke-HeadedEditor {
    param(
        [string]$ExecCmds,
        [string]$ExpectedRunJsonlPath = ''
    )
    $upEsc = Escape-Win32QuotedArgContent $UProject
    $ecEsc = Escape-Win32QuotedArgContent $ExecCmds
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $EditorExe
    $psi.WorkingDirectory = $ProjectRoot
    $psi.UseShellExecute = $false
    $psi.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Minimized
    $psi.Arguments = '"' + $upEsc + '" -unattended -nop4 -NoSplash -ExecCmds="' + $ecEsc + '" -log'

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

    function Wait-ForEditorExitOrKill {
        param([System.Diagnostics.Process]$Proc)
        if ($null -eq $Proc) { return 1 }
        $useWatchdog = ($EditorExitTimeoutSec -gt 0)
        $deadlineUtc = if ($useWatchdog) { (Get-Date).ToUniversalTime().AddSeconds([Math]::Max(1, $EditorExitTimeoutSec)) } else { $null }
        $sawTerminalRun = $false

        if (-not [string]::IsNullOrWhiteSpace($ExpectedRunJsonlPath)) {
            while ($true) {
                if ($Proc.HasExited) { break }
                if (Test-RunJsonlFinished -RunJsonlPath $ExpectedRunJsonlPath) {
                    $sawTerminalRun = $true
                    break
                }
                if ($useWatchdog -and ((Get-Date).ToUniversalTime() -ge $deadlineUtc)) {
                    break
                }
                Start-Sleep -Milliseconds 200
            }
            if ($sawTerminalRun -and -not $Proc.HasExited) {
                try { [void]$Proc.CloseMainWindow() } catch {}
                if (-not $Proc.WaitForExit(3000)) {
                    try { $Proc.Kill() } catch {}
                    try { $Proc.WaitForExit() } catch {}
                }
                Write-Host "UnrealEditor exit code: $($Proc.ExitCode)" -ForegroundColor $(if ($Proc.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
                return $Proc.ExitCode
            }
        }

        if (-not $Proc.HasExited) {
            if ($useWatchdog) {
                Write-Warning "UnrealEditor did not reach terminal state within $EditorExitTimeoutSec seconds; force-killing for fast iteration."
                try { $Proc.Kill() } catch {}
                try { $Proc.WaitForExit() } catch {}
            } else {
                [void]$Proc.WaitForExit()
            }
        }
        Write-Host "UnrealEditor exit code: $($Proc.ExitCode)" -ForegroundColor $(if ($Proc.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
        return $Proc.ExitCode
    }

    if ($DumpContext) {
        $prevDump = $env:UNREAL_AI_HARNESS_DUMP_CONTEXT
        $env:UNREAL_AI_HARNESS_DUMP_CONTEXT = '1'
        try {
            $prevForeground = [UnrealWindowFocus]::GetForegroundWindow()
            $p = [System.Diagnostics.Process]::Start($psi)
            if ($p) { $Script:SpawnedEditorPids += $p.Id }
            if ($p) { Minimize-ProcessMainWindowNoActivate -Proc $p -PreviousForeground $prevForeground }
            return (Wait-ForEditorExitOrKill $p)
        }
        catch {
            Write-Warning ("Failed to start UnrealEditor: {0}" -f $_.Exception.Message)
            return -1001
        }
        finally {
            if ($null -ne $prevDump -and $prevDump -ne '') {
                $env:UNREAL_AI_HARNESS_DUMP_CONTEXT = $prevDump
            }
            else {
                Remove-Item Env:\UNREAL_AI_HARNESS_DUMP_CONTEXT -ErrorAction SilentlyContinue
            }
        }
    }
    else {
        try {
            $prevForeground = [UnrealWindowFocus]::GetForegroundWindow()
            $p = [System.Diagnostics.Process]::Start($psi)
            if ($p) { $Script:SpawnedEditorPids += $p.Id }
            if ($p) { Minimize-ProcessMainWindowNoActivate -Proc $p -PreviousForeground $prevForeground }
            return (Wait-ForEditorExitOrKill $p)
        }
        catch {
            Write-Warning ("Failed to start UnrealEditor: {0}" -f $_.Exception.Message)
            return -1001
        }
    }
}

function Stop-AllUnrealEditors {
    $procs = Get-Process UnrealEditor -ErrorAction SilentlyContinue
    if ($procs) {
        Write-Warning ("Detected {0} pre-existing UnrealEditor process(es); terminating for deterministic live runs." -f $procs.Count)
        $procs | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }
}

if ($KillPreExistingEditors) {
    Stop-AllUnrealEditors
}

if (-not $SkipMatrix) {
    if ([string]::IsNullOrWhiteSpace($MatrixFilter)) {
        $matrixCmd = 'UnrealAi.RunCatalogMatrix'
    }
    else {
        $matrixCmd = "UnrealAi.RunCatalogMatrix $MatrixFilter"
    }
    $matrixExec = "$matrixCmd,QUIT_EDITOR"
    Write-Host "Catalog matrix (headed): $matrixExec" -ForegroundColor Cyan
    [void](Invoke-HeadedEditor $matrixExec)
}

if ($SingleSession) {
    Write-Warning 'SingleSession is unsupported when each scenario uses UNREAL_AI_HARNESS_MESSAGE_FILE (env cannot change mid-ExecCmds). Running one editor launch per scenario.'
}

foreach ($s in $scenarios) {
    $msgPath = Join-Path $manifestDir ([string]$s.message_file)
    $msgAbs = $msgPath
    if (-not (Test-Path -LiteralPath $msgAbs)) {
        Write-Error "Message file not found: $msgAbs"
    }
    $msgAbs = (Resolve-Path -LiteralPath $msgAbs).Path
    $mode = if ($s.mode) { [string]$s.mode } else { 'agent' }
    $dest = Join-Path $outRoot ([string]$s.id)
    if (Test-Path -LiteralPath $dest) {
        Remove-Item -LiteralPath $dest -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null

    $maxInfraRetries = [Math]::Max(0, $InfraRetries)
    $attempt = 0
    $scenarioStatus = [ordered]@{
        scenario_id          = [string]$s.id
        message_file         = $msgAbs
        mode                 = $mode
        retry_count          = 0
        attempts             = @()
        artifact_integrity   = 'missing'
        infra_status         = 'unknown'
        agent_status         = 'unknown'
        editor_exit_code     = $null
        run_id               = $null
        run_finished_success = $null
    }

    try {
    do {
        $runAttemptDir = Join-Path $dest ("attempt_{0:00}" -f $attempt)
        New-Item -ItemType Directory -Force -Path $runAttemptDir | Out-Null
        $env:UNREAL_AI_HARNESS_MESSAGE_FILE = $msgAbs
        if ($MaxLlmRounds -gt 0) {
            $env:UNREAL_AI_HARNESS_MAX_LLM_ROUNDS = [string]$MaxLlmRounds
        }
        $env:UNREAL_AI_HARNESS_OUTPUT_DIR = $runAttemptDir
        $execCore = Build-RunAgentTurnExec $msgAbs $mode
        $execCmd = "$execCore,QUIT_EDITOR"
        Write-Host "Scenario $($s.id) attempt ${attempt}: UNREAL_AI_HARNESS_MESSAGE_FILE=$msgAbs | $execCmd" -ForegroundColor Cyan
        $expectedRunJsonl = Join-Path $runAttemptDir 'run.jsonl'
        $exitCode = [int](Invoke-HeadedEditor $execCmd $expectedRunJsonl)
        $runJsonl = Join-Path $runAttemptDir 'run.jsonl'
        if (-not (Test-Path -LiteralPath $runJsonl)) {
            $nestedRun = Get-ChildItem -LiteralPath $runAttemptDir -Recurse -Filter 'run.jsonl' -File -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1
            if ($nestedRun) {
                $runJsonl = $nestedRun.FullName
            }
        }
        $integrity = Get-RunIntegrity $runJsonl
        $ctxIntegrity = Get-ContextDumpIntegrity -RunDir $runAttemptDir -ExpectContextDumps ([bool]$DumpContext)
        $threadId = Get-ThreadIdFromContextTrace -RunDir $runAttemptDir
        $decisionCopiedCount = Copy-ContextDecisionLogsForThread -ProjectRootPath $ProjectRoot -ThreadId $threadId -DestRunDir $runAttemptDir
        $runMetrics = Get-LiveRunMetrics -RunJsonlPath $runJsonl

        $treatAsOk = ($exitCode -eq 0) -or ($integrity.run_finished_present -and $integrity.run_finished_success)

        $attemptStatus = [ordered]@{
            attempt_index        = $attempt
            run_dir              = $runAttemptDir
            editor_exit_code     = $exitCode
            run_jsonl            = $runJsonl
            run_id               = $integrity.run_id
            run_started_present  = $integrity.run_started_present
            run_finished_present = $integrity.run_finished_present
            run_finished_success = $integrity.run_finished_success
            artifact_integrity   = $integrity.artifact_integrity
            context_dump_expected = $ctxIntegrity.expected
            context_dump_count   = $ctxIntegrity.emitted_count
            context_dump_ok      = $ctxIntegrity.ok
            context_decision_thread_id = $threadId
            context_decision_log_files = $decisionCopiedCount
            infra_status         = if ($treatAsOk) { 'editor_exit_ok' } else { 'editor_exit_nonzero' }
            agent_status         = 'unknown'
        }
        if (-not $ctxIntegrity.ok) {
            $attemptStatus.infra_status = 'dump_context_missing'
        }
        $attemptStatus.tool_failure_top = $runMetrics.tool_failure_top
        $attemptStatus.normalization_applied_count = $runMetrics.normalization_applied_count
        $attemptStatus.low_confidence_search_count = $runMetrics.low_confidence_search_count
        $attemptStatus.low_confidence_max_streak = $runMetrics.low_confidence_max_streak
        if ($attemptStatus.infra_status -eq 'editor_exit_ok' -and $integrity.run_finished_present) {
            $attemptStatus.agent_status = if ($integrity.run_finished_success) { 'success' } else { 'error' }
        }
        $scenarioStatus.attempts += $attemptStatus

        $scenarioStatus.retry_count = $attempt
        $scenarioStatus.editor_exit_code = $exitCode
        $scenarioStatus.artifact_integrity = $integrity.artifact_integrity
        $scenarioStatus.infra_status = $attemptStatus.infra_status
        $scenarioStatus.agent_status = $attemptStatus.agent_status
        $scenarioStatus.run_id = $integrity.run_id
        $scenarioStatus.run_finished_success = $integrity.run_finished_success
        if (-not $ctxIntegrity.ok) {
            $scenarioStatus.infra_status = 'dump_context_missing'
        }

        Remove-Item Env:\UNREAL_AI_HARNESS_MESSAGE_FILE -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_MAX_LLM_ROUNDS -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_OUTPUT_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_THREAD_GUID -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_AGENT_MODE -ErrorAction SilentlyContinue

        if ($treatAsOk) {
            break
        }
        if ($attempt -lt $maxInfraRetries) {
            Write-Warning "Scenario $($s.id): infra failure (exit=$exitCode). Retrying with fresh editor launch."
        }
        $attempt++
    } while ($attempt -le $maxInfraRetries)
    }
    catch {
        Write-Warning ("Scenario {0} raised exception: {1}" -f $s.id, $_.Exception.Message)
        $scenarioStatus.infra_status = 'scenario_exception'
        $scenarioStatus.agent_status = 'error'
    }
    finally {
        Remove-Item Env:\UNREAL_AI_HARNESS_MESSAGE_FILE -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_MAX_LLM_ROUNDS -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_OUTPUT_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_THREAD_GUID -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_AGENT_MODE -ErrorAction SilentlyContinue
        $statusPath = Join-Path $dest 'scenario_status.json'
        ($scenarioStatus | ConvertTo-Json -Depth 8) + "`n" | Set-Content -LiteralPath $statusPath -Encoding UTF8
    }
}

Write-Host "Done. Review bundle: python tests\bundle_live_harness_review.py `"$outRoot`"" -ForegroundColor Cyan
Stop-SpawnedEditors
Release-RunLock -Path $lockPath
