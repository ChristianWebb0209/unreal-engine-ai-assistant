#requires -Version 5.1
<#
  Long-running headed harness runner for bulk conversational suites.

  Purpose
  - Run many multi-turn conversations in headed Unreal Editor using UnrealAi.RunAgentTurn.
  - Keep each JSON file's prompts on one stable thread id (context accumulates across turns).
  - Emit detailed artifacts next to the test data folder so iterative analysis is easy.

  Usage (from repo root):
    .\tests\long-running-tests\run-long-running-headed.ps1 -ScenarioFolder my-suite
    .\tests\long-running-tests\run-long-running-headed.ps1 -ScenarioFolder tests\long-running-tests\my-suite
    .\tests\long-running-tests\run-long-running-headed.ps1 -ScenarioFolder my-suite -DryRun

  JSON schema (one file can contain many turns):
  {
    "suite_id": "optional_suite_name",
    "default_type": "agent",
    "turns": [
      { "type": "ask",   "request": "What is currently selected?" },
      { "type": "agent", "request": "Open the selected blueprint and inspect it." },
      { "type": "plan",  "request": "Plan a refactor in small steps." }
    ]
  }

  Notes
  - "type" values allowed: ask, agent, plan.
  - "plan" is mapped to harness mode "orchestrate".
  - One .json file = one headed editor run with many consecutive RunAgentTurn commands.
  - Output location:
      <scenario-folder>\runs\<json-name>\run_<timestamp>\
        - summary.json
        - workflow-input.json (copy of source)
        - thread_id.txt
        - turn_messages\turn_XX.txt
        - turns\step_XX\run.jsonl (+ context dumps)
        - turns\step_XX\context_decision_logs\*.jsonl

  Context observability defaults (enabled unless already set)
  - UNREAL_AI_HARNESS_DUMP_CONTEXT=1
  - UNREAL_AI_CONTEXT_DECISION_LOG=1
  - UNREAL_AI_CONTEXT_VERBOSE=1

  Important
  - Candidate "considered but not selected" details depend on plugin-side decision logging.
    If you want richer "near misses" than current logs provide, extend context decision events
    in plugin context ranking code and this runner will capture those files automatically.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ScenarioFolder,
    [string]$EngineRoot = '',
    [string]$ProjectRoot = '',
    [int]$MaxLlmRounds = 8,
    [int]$SyncWaitMs = 60000,
    [int]$HttpTimeoutSec = 20,
    [int]$EditorExitTimeoutSec = 0,
    [switch]$CloseAllUnrealEditorsOnExit,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$Script:SpawnedEditorPids = @()
$Script:RunStartUtc = (Get-Date).ToUniversalTime()

$RepoRootForEnv = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $RepoRootForEnv 'scripts\Import-RepoDotenv.ps1')
Import-RepoDotenv -RepoRoot $RepoRootForEnv
. (Join-Path $RepoRootForEnv 'scripts\Set-UnrealAiHeadedToolPackDefaults.ps1')

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = $RepoRootForEnv
}
if (-not (Test-Path (Join-Path $ProjectRoot 'blank.uproject'))) {
    Write-Error "Could not locate blank.uproject under ProjectRoot=$ProjectRoot"
}
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }
}

$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$UProject = Join-Path $ProjectRoot 'blank.uproject'
if (-not (Test-Path $EditorExe)) {
    Write-Error "UnrealEditor.exe not found: $EditorExe (set UE_ENGINE_ROOT or pass -EngineRoot)."
}

if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_HARNESS_DUMP_CONTEXT)) {
    $env:UNREAL_AI_HARNESS_DUMP_CONTEXT = '1'
}
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_CONTEXT_DECISION_LOG)) {
    $env:UNREAL_AI_CONTEXT_DECISION_LOG = '1'
}
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_CONTEXT_VERBOSE)) {
    $env:UNREAL_AI_CONTEXT_VERBOSE = '1'
}
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_LLM_STREAM)) {
    $env:UNREAL_AI_LLM_STREAM = '0'
}
$env:UNREAL_AI_HARNESS_SYNC_WAIT_MS = [string]([Math]::Max(10000, $SyncWaitMs))
$env:UNREAL_AI_HTTP_REQUEST_TIMEOUT_SEC = [string]([Math]::Max(5, $HttpTimeoutSec))

function Resolve-ScenarioFolder {
    param([string]$PathLike)
    if ([string]::IsNullOrWhiteSpace($PathLike)) {
        Write-Error 'ScenarioFolder is required.'
    }
    if ([System.IO.Path]::IsPathRooted($PathLike)) {
        return (Resolve-Path -LiteralPath $PathLike).Path
    }
    $base = Join-Path $ProjectRoot 'tests\long-running-tests'
    $candidate = Join-Path $base $PathLike
    if (Test-Path -LiteralPath $candidate) {
        return (Resolve-Path -LiteralPath $candidate).Path
    }
    if (Test-Path -LiteralPath $PathLike) {
        return (Resolve-Path -LiteralPath $PathLike).Path
    }
    Write-Error "ScenarioFolder not found: $PathLike"
}

function Map-TypeToMode {
    param([string]$TypeValue, [string]$FallbackType)
    $t = if ([string]::IsNullOrWhiteSpace($TypeValue)) { $FallbackType } else { $TypeValue }
    $v = $t.Trim().ToLowerInvariant()
    switch ($v) {
        'ask' { return 'ask' }
        'agent' { return 'agent' }
        'plan' { return 'orchestrate' }
        default { throw "Unsupported type '$t'. Allowed: ask, agent, plan." }
    }
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
    $count = 0
    $files = Get-ChildItem -LiteralPath $src -File -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -like '*.jsonl' -or $_.Name -like '*-summary.md'
    }
    foreach ($f in $files) {
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $dest $f.Name) -Force
        $count++
    }
    return $count
}

function Test-RunJsonlFinished {
    param([string]$RunJsonlPath)
    if ([string]::IsNullOrWhiteSpace($RunJsonlPath)) { return $false }
    if (-not (Test-Path -LiteralPath $RunJsonlPath)) { return $false }
    try {
        $raw = Get-Content -LiteralPath $RunJsonlPath -Raw -Encoding UTF8 -ErrorAction Stop
        return $raw -match '"type"\s*:\s*"run_finished"'
    }
    catch {
        return $false
    }
}

function Stop-TrackedEditors {
    foreach ($pid in @($Script:SpawnedEditorPids)) {
        try {
            $proc = Get-Process -Id $pid -ErrorAction SilentlyContinue
            if ($proc) {
                try { [void]$proc.CloseMainWindow() } catch {}
                if (-not $proc.WaitForExit(1500)) {
                    try { $proc.Kill() } catch {}
                    try { $proc.WaitForExit(4000) } catch {}
                }
            }
        } catch {}
    }
}

function Stop-AllUnrealEditorsHard {
    $all = Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue
    foreach ($p in @($all)) {
        try {
            try { [void]$p.CloseMainWindow() } catch {}
            if (-not $p.WaitForExit(1500)) {
                try { $p.Kill() } catch {}
            }
        } catch {}
    }
}

function Invoke-HeadedEditorDynamic {
    param(
        [string]$ExecCmds,
        [string[]]$ExpectedRunJsonls,
        [int]$TurnCount
    )
    $args = @(
        "`"$UProject`"",
        '-nop4',
        '-nosplash',
        '-log',
        '-unattended',
        '-LogCmds=LogAutomationController Verbose, LogAutomationCommandLine Verbose, LogUnrealAiHarness Verbose',
        "-ExecCmds=`"$ExecCmds`""
    )

    $useWatchdog = ($EditorExitTimeoutSec -gt 0)
    $watchdogDeadlineUtc = if ($useWatchdog) { (Get-Date).ToUniversalTime().AddSeconds($EditorExitTimeoutSec) } else { $null }
    Write-Host ("Running headed editor... mode={0}" -f $(if ($useWatchdog) { "dynamic+watchdog" } else { "dynamic-no-watchdog" })) -ForegroundColor Cyan
    $p = Start-Process -FilePath $EditorExe -ArgumentList $args -PassThru -NoNewWindow
    $Script:SpawnedEditorPids += $p.Id

    $allFinished = $false
    while ($true) {
        if ($p.HasExited) { break }
        $now = (Get-Date).ToUniversalTime()
        $done = 0
        foreach ($j in $ExpectedRunJsonls) {
            if (Test-RunJsonlFinished -RunJsonlPath $j) { $done++ }
        }
        if ($ExpectedRunJsonls.Count -gt 0 -and $done -ge $ExpectedRunJsonls.Count) {
            $allFinished = $true
            break
        }
        if ($useWatchdog -and $now -ge $watchdogDeadlineUtc) {
            Write-Warning ("Watchdog timeout waiting for run completion ({0}/{1} turns finished). Forcing shutdown." -f $done, $ExpectedRunJsonls.Count)
            break
        }
        Start-Sleep -Milliseconds 200
    }

    if (-not $p.HasExited) {
        try { [void]$p.CloseMainWindow() } catch {}
        if (-not $p.WaitForExit(2500)) {
            try { $p.Kill() } catch {}
            try { $p.WaitForExit(6000) } catch {}
        }
    }
    if (-not $p.HasExited) {
        try { $p.Kill() } catch {}
    }

    $exitCode = if ($p.HasExited) { [int]$p.ExitCode } else { -1003 }
    return [ordered]@{
        exit_code = $exitCode
        all_finished = $allFinished
    }
}

$scenarioAbs = Resolve-ScenarioFolder $ScenarioFolder
$jsonFiles = Get-ChildItem -LiteralPath $scenarioAbs -File -Filter '*.json' | Sort-Object Name
if ($jsonFiles.Count -eq 0) {
    Write-Error "No .json scenario files in: $scenarioAbs"
}

$allStatuses = @()
try {
foreach ($jf in $jsonFiles) {
    $raw = Get-Content -LiteralPath $jf.FullName -Raw -Encoding UTF8
    $doc = $raw | ConvertFrom-Json
    $turns = @($doc.turns)
    if ($turns.Count -eq 0) {
        Write-Warning "Skipping $($jf.Name): no turns[]"
        continue
    }

    $suiteId = if ($doc.suite_id) { [string]$doc.suite_id } else { 'long_running_suite' }
    $defaultType = if ($doc.default_type) { [string]$doc.default_type } else { 'agent' }
    $threadId = [guid]::NewGuid().ToString()
    $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')

    $fileBase = [System.IO.Path]::GetFileNameWithoutExtension($jf.Name)
    $runRoot = Join-Path $scenarioAbs ("runs\{0}\run_{1}" -f $fileBase, $stamp)
    $turnsRoot = Join-Path $runRoot 'turns'
    $msgRoot = Join-Path $runRoot 'turn_messages'
    New-Item -ItemType Directory -Force -Path $turnsRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $msgRoot | Out-Null
    Copy-Item -LiteralPath $jf.FullName -Destination (Join-Path $runRoot 'workflow-input.json') -Force
    [System.IO.File]::WriteAllText((Join-Path $runRoot 'thread_id.txt'), $threadId, (New-Object System.Text.UTF8Encoding($false)))

    $execParts = @()
    $turnStatus = @()
    $i = 0
    foreach ($t in $turns) {
        $i++
        $req = [string]$t.request
        if ([string]::IsNullOrWhiteSpace($req)) {
            throw "File $($jf.Name): turn #$i missing request"
        }
        $mode = Map-TypeToMode -TypeValue ([string]$t.type) -FallbackType $defaultType
        $msgPath = Join-Path $msgRoot ("turn_{0:D2}.txt" -f $i)
        $stepDir = Join-Path $turnsRoot ("step_{0:D2}" -f $i)
        New-Item -ItemType Directory -Force -Path $stepDir | Out-Null
        [System.IO.File]::WriteAllText($msgPath, $req, (New-Object System.Text.UTF8Encoding($false)))
        $execParts += ('UnrealAi.RunAgentTurn "{0}" "{1}" "{2}" "{3}"' -f $msgPath, $threadId, $mode, $stepDir)
        $turnStatus += [ordered]@{
            turn_index = $i
            type = if ($t.type) { [string]$t.type } else { $defaultType }
            mode = $mode
            request = $req
            message_file = $msgPath
            step_dir = $stepDir
        }
    }

    $summary = [ordered]@{
        suite_id = $suiteId
        source_file = $jf.FullName
        run_root = $runRoot
        thread_id = $threadId
        started_utc = (Get-Date).ToUniversalTime().ToString('o')
        turn_count = $turnStatus.Count
        max_llm_rounds = $MaxLlmRounds
        editor_exit_code = $null
        dry_run = [bool]$DryRun
        turns = $turnStatus
    }

    if ($DryRun) {
        $summary.editor_exit_code = 0
        $summary.all_turns_reached_terminal = $true
        $summary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')
        ($summary | ConvertTo-Json -Depth 8) + "`n" | Set-Content -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding UTF8
        $allStatuses += $summary
        Write-Host "DryRun prepared: $($jf.Name) ($($turnStatus.Count) turns)" -ForegroundColor Yellow
        continue
    }

    $env:UNREAL_AI_HARNESS_MAX_LLM_ROUNDS = [string]([Math]::Max(1, $MaxLlmRounds))
    $execCmds = ($execParts -join ',') + ',Quit'
    $exitCode = -1
    $dynamicDone = $false
    $expectedRunJsonls = @($turnStatus | ForEach-Object { Join-Path $_.step_dir 'run.jsonl' })
    try {
        Write-Host ("Scenario: {0} | turns={1} | thread={2}" -f $jf.Name, $turnStatus.Count, $threadId) -ForegroundColor Green
        $runResult = Invoke-HeadedEditorDynamic -ExecCmds $execCmds -ExpectedRunJsonls $expectedRunJsonls -TurnCount $turnStatus.Count
        $exitCode = [int]$runResult.exit_code
        $dynamicDone = [bool]$runResult.all_finished
    }
    finally {
        Remove-Item Env:\UNREAL_AI_HARNESS_MAX_LLM_ROUNDS -ErrorAction SilentlyContinue
    }

    foreach ($ts in $turnStatus) {
        $ts.run_jsonl_exists = Test-Path -LiteralPath (Join-Path $ts.step_dir 'run.jsonl')
        $ts.context_files = @(Get-ChildItem -LiteralPath $ts.step_dir -File -Filter 'context_window_*.txt' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
        $ts.context_decision_logs_copied = Copy-ContextDecisionLogsForThread -ProjectRootPath $ProjectRoot -ThreadId $threadId -DestRunDir $ts.step_dir
    }

    $summary.editor_exit_code = $exitCode
    $summary.all_turns_reached_terminal = $dynamicDone
    $summary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')
    ($summary | ConvertTo-Json -Depth 8) + "`n" | Set-Content -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding UTF8
    $allStatuses += $summary
}
}
finally {
    Stop-TrackedEditors
    if ($CloseAllUnrealEditorsOnExit) {
        Stop-AllUnrealEditorsHard
    }
}

$suiteSummary = [ordered]@{
    scenario_folder = $scenarioAbs
    started_utc = (Get-Date).ToUniversalTime().ToString('o')
    files_processed = $allStatuses.Count
    runs = $allStatuses
}
($suiteSummary | ConvertTo-Json -Depth 8) + "`n" | Set-Content -LiteralPath (Join-Path $scenarioAbs 'last-suite-summary.json') -Encoding UTF8

$failed = @($allStatuses | Where-Object { [int]$_.editor_exit_code -ne 0 }).Count
Write-Host ("Completed files={0} failed_editor_exit={1}" -f $allStatuses.Count, $failed) -ForegroundColor Cyan
if ($failed -gt 0) { exit 1 }
exit 0
