#requires -Version 5.1
<#
  Context manager qualitative tier: headed Unreal Editor, multi-turn workflows with a STABLE thread id
  per workflow so context persists like Agent Chat. Copies each harness run to
  tests/out/context_runs/<suite_id>/<workflow_id>/step_<nn>_<step_id>/.

  Use -SuiteManifest tests\context_workflows\suite.json or -WorkflowManifest for one workflow.
  Set -DumpContext or UNREAL_AI_HARNESS_DUMP_CONTEXT=1 for context_window_*.txt dumps.

  Do NOT set UNREAL_AI_LLM_FIXTURE for realistic runs (use -AllowFixture to override).

  See docs\AGENT_HARNESS_HANDOFF.md and tests\context_workflows\README.md
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = '',
    [string]$ProjectRoot = '',
    [string]$SuiteManifest = '',
    [string]$WorkflowManifest = '',
    [int]$MaxWorkflows = 0,
    [int]$MaxSteps = 0,
    [switch]$SkipMatrix,
    [string]$MatrixFilter = '',
    [switch]$SingleSession,
    [switch]$DumpContext,
    [switch]$ContextVerbose,
    [switch]$DryRun,
    [switch]$AllowFixture,
    [switch]$TakeoverLock,
    [int]$MaxLlmRounds = 4,
    [int]$EditorExitTimeoutSec = 0,
    [int]$EditorExitGraceSec = 30,
    [int]$InfraRetries = 0
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

$UProject = Join-Path $ProjectRoot 'blank.uproject'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $EditorExe)) {
    Write-Error "UnrealEditor.exe not found: $EditorExe (set UE_ENGINE_ROOT or -EngineRoot)"
}

if (-not $AllowFixture) {
    if ($env:UNREAL_AI_LLM_FIXTURE) {
        Write-Warning "UNREAL_AI_LLM_FIXTURE was set - clearing for live API (use -AllowFixture to keep)."
        Remove-Item Env:\UNREAL_AI_LLM_FIXTURE -ErrorAction SilentlyContinue
    }
}
elseif (-not $env:UNREAL_AI_LLM_FIXTURE) {
    Write-Warning '-AllowFixture passed but UNREAL_AI_LLM_FIXTURE is unset.'
}

if ($WorkflowManifest -and $SuiteManifest) {
    Write-Error "Specify only one of -WorkflowManifest or -SuiteManifest."
}
if (-not $WorkflowManifest -and -not $SuiteManifest) {
    $SuiteManifest = Join-Path $ProjectRoot 'tests\context_workflows\suite.json'
}

$runsRoot = Join-Path $ProjectRoot 'Saved\UnrealAiEditor\HarnessRuns'

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
                throw "Another context workflow runner is active (pid=$($existing.pid), started=$($existing.started_utc))."
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

function Get-HarnessRunDirs {
    param([string]$Root)
    if (-not (Test-Path $Root)) { return @() }
    Get-ChildItem -Path $Root -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending
}

function Copy-LatestHarnessToDir {
    param(
        [string]$Root,
        [string]$DestDir,
        [Nullable[datetime]]$NotBeforeUtc = $null
    )
    $result = [ordered]@{
        copied = $false
        source_name = $null
        source_last_write_utc = $null
        source_is_fresh = $true
    }
    $latest = Get-HarnessRunDirs $Root | Select-Object -First 1
    if (-not $latest) {
        Write-Warning "No harness run under $Root"
        return $result
    }
    $result.source_name = $latest.Name
    $result.source_last_write_utc = $latest.LastWriteTimeUtc.ToString('o')
    if ($NotBeforeUtc.HasValue) {
        $minFreshUtc = $NotBeforeUtc.Value.AddSeconds(-2)
        if ($latest.LastWriteTimeUtc -lt $minFreshUtc) {
            $result.source_is_fresh = $false
            Write-Warning ("Latest harness run appears stale for this attempt: {0} (last_write_utc={1}, expected_after={2})" -f $latest.Name, $latest.LastWriteTimeUtc.ToString('o'), $NotBeforeUtc.Value.ToString('o'))
        }
    }
    if (Test-Path $DestDir) {
        Remove-Item -LiteralPath $DestDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DestDir) | Out-Null
    Copy-Item -LiteralPath $latest.FullName -Destination $DestDir -Recurse
    Write-Host "  Copied $($latest.Name) -> $DestDir" -ForegroundColor Green
    $result.copied = $true
    return $result
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
    if (Test-Path -LiteralPath $dest) {
        Remove-Item -LiteralPath $dest -Recurse -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $copied = 0
    $files = Get-ChildItem -LiteralPath $src -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '*.jsonl' -or $_.Name -like '*-summary.md' }
    foreach ($f in $files) {
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $dest $f.Name) -Force
        $copied++
    }
    return $copied
}

function Resolve-RunJsonlPath {
    param([string]$RunDir)
    $runJsonl = Join-Path $RunDir 'run.jsonl'
    if (Test-Path -LiteralPath $runJsonl) {
        return $runJsonl
    }
    $nested = Get-ChildItem -LiteralPath $RunDir -Recurse -Filter 'run.jsonl' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($nested) {
        return $nested.FullName
    }
    return $runJsonl
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

function Ensure-RunJsonlTerminalRecord {
    param(
        [string]$RunJsonlPath,
        [int]$EditorExitCode
    )
    if (-not (Test-Path -LiteralPath $RunJsonlPath)) {
        return $false
    }

    $integrity = Get-RunIntegrity $RunJsonlPath
    if ($integrity.run_finished_present) {
        return $false
    }
    if (-not $integrity.run_started_present) {
        return $false
    }

    $reason = "Missing run_finished record; synthesized by harness runner fail-safe (editor_exit_code=$EditorExitCode)."
    $obj = [ordered]@{
        type = 'run_finished'
        success = $false
        error_message = $reason
    }
    $line = ($obj | ConvertTo-Json -Compress)
    Add-Content -LiteralPath $RunJsonlPath -Value $line -Encoding UTF8
    Write-Warning "Synthesized terminal run_finished in run.jsonl due to missing sink completion."
    return $true
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
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $EditorExe
    $psi.WorkingDirectory = $ProjectRoot
    $psi.UseShellExecute = $false
    $upEsc = Escape-Win32QuotedArgContent $UProject
    $ecEsc = Escape-Win32QuotedArgContent $ExecCmds
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

        # Event-driven path: as soon as run.jsonl reaches terminal record, proceed.
        if (-not [string]::IsNullOrWhiteSpace($ExpectedRunJsonlPath)) {
            while ($true) {
                if ($Proc.HasExited) {
                    break
                }
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
                Write-Host "  UnrealEditor exit: $($Proc.ExitCode)" -ForegroundColor $(if ($Proc.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
                return $Proc.ExitCode
            }
        }

        if (-not $Proc.HasExited) {
            if ($useWatchdog) {
                Write-Warning "UnrealEditor did not reach terminal state within $EditorExitTimeoutSec seconds; waiting an additional grace period of $EditorExitGraceSec seconds before force-kill."
                $graceMs = [Math]::Max(1000, $EditorExitGraceSec * 1000)
                if (-not $Proc.WaitForExit($graceMs)) {
                    Write-Warning "UnrealEditor still running after grace period; attempting graceful close before force-kill."
                    try { [void]$Proc.CloseMainWindow() } catch {}
                    if (-not $Proc.WaitForExit(15000)) {
                        Write-Warning "UnrealEditor did not close gracefully; force-killing for fast iteration."
                        try { $Proc.Kill() } catch {}
                        try { $Proc.WaitForExit() } catch {}
                    }
                }
            } else {
                # No arbitrary timeout mode: wait until editor exits naturally.
                [void]$Proc.WaitForExit()
            }
        }
        Write-Host "  UnrealEditor exit: $($Proc.ExitCode)" -ForegroundColor $(if ($Proc.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
        return $Proc.ExitCode
    }

    $run = {
        $prevForeground = [UnrealWindowFocus]::GetForegroundWindow()
        $proc = [System.Diagnostics.Process]::Start($psi)
        if (-not $proc) {
            Write-Error "Failed to start UnrealEditor: $EditorExe"
        }
        $Script:SpawnedEditorPids += $proc.Id
        Minimize-ProcessMainWindowNoActivate -Proc $proc -PreviousForeground $prevForeground
        return (Wait-ForEditorExitOrKill $proc)
    }

    $prevDump = $null
    $prevVerbose = $null
    $didSetDump = $false
    $didSetVerbose = $false
    if ($DumpContext) {
        $prevDump = $env:UNREAL_AI_HARNESS_DUMP_CONTEXT
        $env:UNREAL_AI_HARNESS_DUMP_CONTEXT = '1'
        $didSetDump = $true
    }
    if ($ContextVerbose) {
        $prevVerbose = $env:UNREAL_AI_CONTEXT_VERBOSE
        $env:UNREAL_AI_CONTEXT_VERBOSE = '1'
        $didSetVerbose = $true
    }
    try {
        return & $run
    }
    finally {
        if ($didSetDump) {
            if ($null -ne $prevDump -and $prevDump -ne '') {
                $env:UNREAL_AI_HARNESS_DUMP_CONTEXT = $prevDump
            }
            else {
                Remove-Item Env:\UNREAL_AI_HARNESS_DUMP_CONTEXT -ErrorAction SilentlyContinue
            }
        }
        if ($didSetVerbose) {
            if ($null -ne $prevVerbose -and $prevVerbose -ne '') {
                $env:UNREAL_AI_CONTEXT_VERBOSE = $prevVerbose
            }
            else {
                Remove-Item Env:\UNREAL_AI_CONTEXT_VERBOSE -ErrorAction SilentlyContinue
            }
        }
    }
}

function Set-ContextHarnessStepEnv {
    param(
        [string]$MsgAbs,
        [string]$ThreadGuid,
        [string]$Mode,
        [string]$OutputDir
    )
    $env:UNREAL_AI_HARNESS_MESSAGE_FILE = $MsgAbs
    $env:UNREAL_AI_HARNESS_THREAD_GUID = $ThreadGuid
    $env:UNREAL_AI_HARNESS_OUTPUT_DIR = $OutputDir
    Remove-Item Env:\UNREAL_AI_HARNESS_AGENT_MODE -ErrorAction SilentlyContinue
    $m = if ($Mode) { $Mode.ToLowerInvariant() } else { 'agent' }
    if ($m -ne 'agent') {
        $env:UNREAL_AI_HARNESS_AGENT_MODE = $m
    }
}

function Clear-ContextHarnessStepEnv {
    Remove-Item Env:\UNREAL_AI_HARNESS_MESSAGE_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_THREAD_GUID -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_OUTPUT_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_AGENT_MODE -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_MAX_LLM_ROUNDS -ErrorAction SilentlyContinue
}

function Resolve-WorkflowPath {
    param(
        [string]$BaseDir,
        [string]$RelativeOrAbsolute
    )
    if ([System.IO.Path]::IsPathRooted($RelativeOrAbsolute)) {
        return $RelativeOrAbsolute
    }
    return (Join-Path $BaseDir $RelativeOrAbsolute)
}

function Resolve-RepoPath {
    param([string]$PathLike)
    if ([string]::IsNullOrWhiteSpace($PathLike)) {
        return $null
    }
    if (Test-Path -LiteralPath $PathLike) {
        return (Resolve-Path -LiteralPath $PathLike).Path
    }
    $joined = Join-Path $ProjectRoot $PathLike
    if (Test-Path -LiteralPath $joined) {
        return (Resolve-Path -LiteralPath $joined).Path
    }
    Write-Error "Path not found: $PathLike"
}

# Build list of (workflowJsonPath, workflowBaseDir)
$workflowJobs = @()
if ($WorkflowManifest) {
    $wfPath = Resolve-RepoPath $WorkflowManifest
    $workflowJobs += @{
        Path    = $wfPath
        BaseDir = Split-Path -Parent $wfPath
    }
}
else {
    $suitePath = Resolve-RepoPath $SuiteManifest
    $suiteDir = Split-Path -Parent $suitePath
    $suiteRaw = Get-Content -LiteralPath $suitePath -Raw -Encoding UTF8
    $suiteDoc = $suiteRaw | ConvertFrom-Json
    foreach ($rel in @($suiteDoc.workflows)) {
        $rp = Resolve-WorkflowPath $suiteDir ([string]$rel)
        if (-not (Test-Path -LiteralPath $rp)) {
            Write-Error "Workflow file not found: $rp"
        }
        $workflowJobs += @{
            Path    = $rp
            BaseDir = Split-Path -Parent $rp
        }
    }
}

if ($MaxWorkflows -gt 0 -and $workflowJobs.Count -gt $MaxWorkflows) {
    $workflowJobs = $workflowJobs[0..($MaxWorkflows - 1)]
}

New-Item -ItemType Directory -Force -Path (Join-Path $ProjectRoot 'Saved\UnrealAiEditor') | Out-Null
$lockPath = Join-Path $ProjectRoot 'Saved\UnrealAiEditor\context_workflows.lock.json'
Acquire-RunLock -Path $lockPath -Takeover:$TakeoverLock

try {
if ($DryRun) {
    Write-Host "Dry run: $($workflowJobs.Count) workflow(s)" -ForegroundColor Cyan
    foreach ($job in $workflowJobs) {
        $raw = Get-Content -LiteralPath $job.Path -Raw -Encoding UTF8
        $w = $raw | ConvertFrom-Json
        $steps = @($w.steps)
        if ($MaxSteps -gt 0 -and $steps.Count -gt $MaxSteps) {
            $steps = $steps[0..($MaxSteps - 1)]
        }
        Write-Host ("  Workflow: {0} ({1} steps)" -f $w.workflow_id, $steps.Count)
        $si = 0
        foreach ($st in $steps) {
            $si++
            $mp = Join-Path $job.BaseDir ([string]$st.message_file)
            Write-Host ("    [{0}] {1} -> {2}" -f $si, $st.id, $mp)
        }
    }
    return
}

if (-not $SkipMatrix) {
    if ([string]::IsNullOrWhiteSpace($MatrixFilter)) {
        $matrixCmd = 'UnrealAi.RunCatalogMatrix'
    }
    else {
        $matrixCmd = "UnrealAi.RunCatalogMatrix $MatrixFilter"
    }
    Write-Host "Catalog matrix (headed): $matrixCmd,Quit" -ForegroundColor Cyan
    [void](Invoke-HeadedEditor "$matrixCmd,Quit")
}

foreach ($job in $workflowJobs) {
    $raw = Get-Content -LiteralPath $job.Path -Raw -Encoding UTF8
    $w = $raw | ConvertFrom-Json
    $suiteId = [string]$w.suite_id
    if ([string]::IsNullOrWhiteSpace($suiteId)) {
        $suiteId = 'default_context_suite'
    }
    $wfId = [string]$w.workflow_id
    $defaultMode = if ($w.default_mode) { [string]$w.default_mode } else { 'agent' }
    $steps = @($w.steps)
    if ($MaxSteps -gt 0 -and $steps.Count -gt $MaxSteps) {
        $steps = $steps[0..($MaxSteps - 1)]
    }

    $threadGuid = [guid]::NewGuid().ToString()
    $wfOut = Join-Path $ProjectRoot "tests\out\context_runs\$suiteId\$wfId"
    New-Item -ItemType Directory -Force -Path $wfOut | Out-Null
    Copy-Item -LiteralPath $job.Path -Destination (Join-Path $wfOut 'workflow.json') -Force
    [System.IO.File]::WriteAllText(
        (Join-Path $wfOut 'thread_id.txt'),
        $threadGuid,
        (New-Object System.Text.UTF8Encoding($false)))

    Write-Host "Workflow $wfId thread=$threadGuid steps=$($steps.Count)" -ForegroundColor Cyan
    $workflowStatus = [ordered]@{
        workflow_id = $wfId
        suite_id = $suiteId
        thread_id = $threadGuid
        started_utc = (Get-Date).ToUniversalTime().ToString('o')
        single_session = [bool]$SingleSession
        step_count = $steps.Count
        steps = @()
        infra_status = 'unknown'
        agent_status = 'unknown'
    }

    if ($SingleSession) {
        $cmds = @()
        $si3 = 0
        foreach ($st in $steps) {
            $si3++
            $msgAbs = (Resolve-Path -LiteralPath (Join-Path $job.BaseDir ([string]$st.message_file))).Path
            if (-not (Test-Path -LiteralPath $msgAbs)) {
                Write-Error "Message file not found: $msgAbs"
            }
            $mode = if ($st.mode) { [string]$st.mode } else { $defaultMode }
            $safeId = ([string]$st.id) -replace '[^a-zA-Z0-9_-]', '_'
            $stepDirRel = "step_{0:D2}_{1}" -f $si3, $safeId
            $dest = Join-Path $wfOut $stepDirRel
            if (Test-Path -LiteralPath $dest) {
                Remove-Item -LiteralPath $dest -Recurse -Force -ErrorAction SilentlyContinue
            }
            $cmds += ('UnrealAi.RunAgentTurn "{0}" "{1}" "{2}" "{3}"' -f $msgAbs, $threadGuid, $mode, $dest)
        }
        $execCmds = ($cmds -join ',') + ',Quit'
        Write-Host "  SingleSession: chained $($steps.Count) turns in one editor run" -ForegroundColor Cyan
        try {
            if ($MaxLlmRounds -gt 0) {
                $env:UNREAL_AI_HARNESS_MAX_LLM_ROUNDS = [string]$MaxLlmRounds
            }
            $singleExit = [int](Invoke-HeadedEditor $execCmds)
            $workflowStatus.infra_status = if ($singleExit -eq 0) { 'editor_exit_ok' } else { 'editor_exit_nonzero' }
            $workflowStatus.agent_status = if ($singleExit -eq 0) { 'unknown' } else { 'error' }
        }
        finally {
            Remove-Item Env:\UNREAL_AI_HARNESS_MAX_LLM_ROUNDS -ErrorAction SilentlyContinue
        }
    }
    else {
        $maxInfraRetries = [Math]::Max(0, $InfraRetries)
        $workflowHadInfraFailure = $false
        $workflowHadAgentFailure = $false
        $seenRunIds = @{}
        $si3 = 0
        foreach ($st in $steps) {
            $si3++
            $msgAbs = (Resolve-Path -LiteralPath (Join-Path $job.BaseDir ([string]$st.message_file))).Path
            if (-not (Test-Path -LiteralPath $msgAbs)) {
                Write-Error "Message file not found: $msgAbs"
            }
            $mode = if ($st.mode) { [string]$st.mode } else { $defaultMode }
            $safeId = ([string]$st.id) -replace '[^a-zA-Z0-9_-]', '_'
            $stepDir = "step_{0:D2}_{1}" -f $si3, $safeId
            $dest = Join-Path $wfOut $stepDir
            $attempt = 0
            $stepStatus = [ordered]@{
                step_id = [string]$st.id
                step_dir = $dest
                message_file = $msgAbs
                mode = $mode
                retry_count = 0
                attempts = @()
                artifact_integrity = 'missing'
                infra_status = 'unknown'
                agent_status = 'unknown'
                editor_exit_code = $null
                run_id = $null
                run_finished_success = $null
            }
            do {
                $attemptStartUtc = (Get-Date).ToUniversalTime()
                if (Test-Path -LiteralPath $dest) {
                    Remove-Item -LiteralPath $dest -Recurse -Force -ErrorAction SilentlyContinue
                }
                New-Item -ItemType Directory -Force -Path $dest | Out-Null
                $expectedRunJsonl = Join-Path $dest 'run.jsonl'
                Set-ContextHarnessStepEnv $msgAbs $threadGuid $mode $dest
                if ($MaxLlmRounds -gt 0) {
                    $env:UNREAL_AI_HARNESS_MAX_LLM_ROUNDS = [string]$MaxLlmRounds
                }
                $exitCode = -1002
                try {
                    Write-Host "  Step $($st.id) attempt ${attempt}: UNREAL_AI_HARNESS_MESSAGE_FILE=$msgAbs | UnrealAi.RunAgentTurn" -ForegroundColor Cyan
                    $exitCode = [int](Invoke-HeadedEditor 'UnrealAi.RunAgentTurn,Quit' $expectedRunJsonl)
                }
                finally {
                    Clear-ContextHarnessStepEnv
                }

                $copyInfo = [ordered]@{
                    copied = $true
                    source_name = 'explicit_output_dir'
                    source_last_write_utc = $attemptStartUtc.ToString('o')
                    source_is_fresh = $true
                }
                $runJsonl = Resolve-RunJsonlPath $dest
                $synthesizedTerminal = Ensure-RunJsonlTerminalRecord -RunJsonlPath $runJsonl -EditorExitCode $exitCode
                $integrity = Get-RunIntegrity $runJsonl
                $ctxIntegrity = Get-ContextDumpIntegrity -RunDir $dest -ExpectContextDumps ([bool]$DumpContext)
                $decisionCopied = Copy-ContextDecisionLogsForThread -ProjectRootPath $ProjectRoot -ThreadId $threadGuid -DestRunDir $dest
                $isDuplicateRunId = $false
                if (-not [string]::IsNullOrWhiteSpace([string]$integrity.run_id)) {
                    if ($seenRunIds.ContainsKey([string]$integrity.run_id)) {
                        $isDuplicateRunId = $true
                    } else {
                        $seenRunIds[[string]$integrity.run_id] = $true
                    }
                }
                if ($decisionCopied -gt 0) {
                    Write-Host ("    copied context decision logs: {0}" -f $decisionCopied) -ForegroundColor DarkGray
                }
                $attemptStatus = [ordered]@{
                    attempt_index = $attempt
                    editor_exit_code = $exitCode
                    run_jsonl = $runJsonl
                    run_id = $integrity.run_id
                    run_started_present = $integrity.run_started_present
                    run_finished_present = $integrity.run_finished_present
                    run_finished_success = $integrity.run_finished_success
                    artifact_integrity = $integrity.artifact_integrity
                    context_dump_expected = $ctxIntegrity.expected
                    context_dump_count = $ctxIntegrity.emitted_count
                    context_dump_ok = $ctxIntegrity.ok
                    harness_source_name = $copyInfo.source_name
                    harness_source_last_write_utc = $copyInfo.source_last_write_utc
                    harness_source_is_fresh = $copyInfo.source_is_fresh
                    synthesized_terminal_record = $synthesizedTerminal
                    duplicate_run_id_seen = $isDuplicateRunId
                    context_decision_log_files = $decisionCopied
                    infra_status = if ($exitCode -eq 0) { 'editor_exit_ok' } else { 'editor_exit_nonzero' }
                    agent_status = 'unknown'
                }
                if (-not $copyInfo.source_is_fresh) {
                    $attemptStatus.infra_status = 'stale_harness_artifact'
                }
                if (-not $ctxIntegrity.ok) {
                    $attemptStatus.infra_status = 'dump_context_missing'
                }
                if ($isDuplicateRunId) {
                    $attemptStatus.infra_status = 'duplicate_run_id'
                }
                if ($integrity.run_finished_present) {
                    $attemptStatus.agent_status = if ($integrity.run_finished_success) { 'success' } else { 'error' }
                }
                if (
                    $attemptStatus.infra_status -ne 'dump_context_missing' -and
                    $attemptStatus.infra_status -ne 'stale_harness_artifact' -and
                    $attemptStatus.infra_status -ne 'duplicate_run_id' -and
                    $exitCode -ne 0 -and
                    $integrity.run_finished_present
                ) {
                    # The editor process may be force-killed after the run finishes; use run.jsonl + required dumps
                    # as the source of truth so we can still proceed to the next workflow step.
                    $attemptStatus.infra_status = 'editor_exit_nonzero_but_run_finished'
                }
                $stepStatus.attempts += $attemptStatus
                $stepStatus.retry_count = $attempt
                $stepStatus.editor_exit_code = $exitCode
                $stepStatus.artifact_integrity = $integrity.artifact_integrity
                $stepStatus.infra_status = $attemptStatus.infra_status
                $stepStatus.agent_status = $attemptStatus.agent_status
                $stepStatus.run_id = $integrity.run_id
                $stepStatus.run_finished_success = $integrity.run_finished_success

                if (
                    $integrity.run_finished_success -and
                    $ctxIntegrity.ok -and
                    $copyInfo.source_is_fresh -and
                    (-not $isDuplicateRunId)
                ) {
                    break
                }
                if ($attempt -lt $maxInfraRetries) {
                    if (-not ($integrity.run_finished_success -and $ctxIntegrity.ok)) {
                        Write-Warning "Step $($st.id): infra failure (exit=$exitCode). Retrying with fresh editor launch."
                    }
                }
                $attempt++
            } while ($attempt -le $maxInfraRetries)

            ($stepStatus | ConvertTo-Json -Depth 8) + "`n" | Set-Content -LiteralPath (Join-Path $dest 'step_status.json') -Encoding UTF8
            $workflowStatus.steps += $stepStatus
            if (
                $stepStatus.infra_status -eq 'dump_context_missing' -or
                $stepStatus.infra_status -eq 'editor_exit_nonzero' -or
                $stepStatus.infra_status -eq 'stale_harness_artifact' -or
                $stepStatus.infra_status -eq 'duplicate_run_id'
            ) {
                $workflowHadInfraFailure = $true
            }
            if ($stepStatus.agent_status -eq 'error') {
                $workflowHadAgentFailure = $true
            }
        }
        $workflowStatus.infra_status = if ($workflowHadInfraFailure) { 'error' } else { 'ok' }
        $workflowStatus.agent_status = if ($workflowHadAgentFailure) { 'error' } else { 'ok_or_unknown' }
    }
    $workflowStatus.completed_utc = (Get-Date).ToUniversalTime().ToString('o')
    ($workflowStatus | ConvertTo-Json -Depth 8) + "`n" | Set-Content -LiteralPath (Join-Path $wfOut 'workflow_status.json') -Encoding UTF8
}

Write-Host "Done. Bundle: python tests\bundle_context_workflow_review.py <tests\out\context_runs\...\workflow_folder>" -ForegroundColor Cyan
}
finally {
    Clear-ContextHarnessStepEnv
    Stop-SpawnedEditors
    Release-RunLock -Path $lockPath
}
