#requires -Version 5.1
<#
  Launch Unreal Editor (headed — real RHI window) and drive console scenarios:
    UnrealAi.RunCatalogMatrix <filter>
    UnrealAi.RunAgentTurn <msg>  (twice; uses UNREAL_AI_LLM_FIXTURE)
  Then exit. Asserts the latest harness run.jsonl with tests/assert_harness_run.py.

  Agent handoff (prompts, catalog, escalation): docs\AGENT_HARNESS_HANDOFF.md

  From repo root:
    .\scripts\run-headed-scenario-smoke.ps1
    .\scripts\run-headed-scenario-smoke.ps1 -MatrixFilter "blueprint" -SkipCatalogMatrix
    .\scripts\run-headed-scenario-smoke.ps1 -LiveApi -SkipCatalogMatrix   # real LLM (+ API cost); uses keys in AI Settings
    $env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'; .\scripts\run-headed-scenario-smoke.ps1
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = '',
    [string]$MatrixFilter = 'blueprint',
    [switch]$SkipCatalogMatrix,
    [switch]$LiveApi,
    [int]$MaxLlmRounds = 2,
    [int]$EditorExitTimeoutSec = 300
)

$ErrorActionPreference = 'Stop'

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

$MsgA = Join-Path $ProjectRoot 'tests\harness_scenarios\user_scenario_a_selection.txt'
$MsgB = Join-Path $ProjectRoot 'tests\harness_scenarios\user_scenario_b_scene_search.txt'
$FixtureRelSelection = 'tests/harness_scenarios/fixture_selection_turn.json'
$FixtureRelScene = 'tests/harness_scenarios/fixture_scene_search_turn.json'

if ($LiveApi) {
    if ($env:UNREAL_AI_LLM_FIXTURE) {
        Write-Warning "UNREAL_AI_LLM_FIXTURE was set ($($env:UNREAL_AI_LLM_FIXTURE)); clearing for -LiveApi."
        Remove-Item Env:\UNREAL_AI_LLM_FIXTURE -ErrorAction SilentlyContinue
    }
    Write-Host 'LiveApi: real LLM; keys from Project Settings > Plugins > Unreal AI Editor. API usage may incur cost.' -ForegroundColor Yellow
}
else {
    # Use per-scenario fixtures because each headed editor launch starts a fresh transport instance.
    $env:UNREAL_AI_LLM_FIXTURE = $FixtureRelSelection
}

if (-not (Test-Path -LiteralPath $MsgA)) { Write-Error "Scenario message file not found: $MsgA" }
if (-not (Test-Path -LiteralPath $MsgB)) { Write-Error "Scenario message file not found: $MsgB" }

if ($LiveApi) {
    Write-Host 'UNREAL_AI_LLM_FIXTURE=(unset)' -ForegroundColor DarkGray
}
else {
    Write-Host "UNREAL_AI_LLM_FIXTURE=(per scenario, starts with $($env:UNREAL_AI_LLM_FIXTURE))" -ForegroundColor DarkGray
}

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

function Stop-AllUnrealEditors {
    $procs = Get-Process UnrealEditor -ErrorAction SilentlyContinue
    if ($procs) {
        Write-Warning ("Detected {0} pre-existing UnrealEditor process(es); terminating for deterministic harness runs." -f $procs.Count)
        $procs | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }
}

function Invoke-HeadedExecCmds {
    param([string]$ExecCmds)
    $upEsc = Escape-Win32QuotedArgContent $UProject
    $ecEsc = Escape-Win32QuotedArgContent $ExecCmds
    $psi.Arguments = '"' + $upEsc + '" -unattended -nop4 -NoSplash -ExecCmds="' + $ecEsc + '" -log'
    $p = [System.Diagnostics.Process]::Start($psi)
    $waitMs = [Math]::Max(1000, $EditorExitTimeoutSec * 1000)
    if (-not $p.WaitForExit($waitMs)) {
        Write-Warning "UnrealEditor did not exit within $EditorExitTimeoutSec seconds; force-killing to keep harness runs moving."
        try { $p.Kill() } catch {}
        try { $p.WaitForExit() } catch {}
    }
    Write-Host "UnrealEditor exit code: $($p.ExitCode)" -ForegroundColor $(if ($p.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
    return $p.ExitCode
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
    if (-not $LiveApi) {
        if ($msg -eq $MsgA) {
            $env:UNREAL_AI_LLM_FIXTURE = $FixtureRelSelection
        } else {
            $env:UNREAL_AI_LLM_FIXTURE = $FixtureRelScene
        }
    }
    if ($MaxLlmRounds -gt 0) {
        $env:UNREAL_AI_HARNESS_MAX_LLM_ROUNDS = [string]$MaxLlmRounds
    }
    try {
        $turnExec = 'UnrealAi.RunAgentTurn,QUIT_EDITOR'
        Write-Host "Headed scenario smoke (turn): UNREAL_AI_HARNESS_MESSAGE_FILE=$($env:UNREAL_AI_HARNESS_MESSAGE_FILE) | $turnExec" -ForegroundColor Cyan
        [void](Invoke-HeadedExecCmds $turnExec)
    }
    finally {
        Remove-Item Env:\UNREAL_AI_HARNESS_MESSAGE_FILE -ErrorAction SilentlyContinue
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

if ($LiveApi) {
    $latest = $runs | Select-Object -First 1
    $jsonl = Join-Path $latest.FullName 'run.jsonl'
    if (-not (Test-Path $jsonl)) {
        Write-Warning "No run.jsonl in $($latest.FullName)"
        exit 1
    }
    Write-Host "Latest artifact: $jsonl" -ForegroundColor Cyan
    Write-Host 'LiveApi: skipping strict assert (tool order differs from fixture). Validate run.jsonl manually.' -ForegroundColor Yellow
    & python (Join-Path $ProjectRoot 'tests\assert_harness_run.py') $jsonl
    if ($LASTEXITCODE -eq 1) {
        exit 1
    }
}
else {
    if ($runs.Count -lt 2) {
        Write-Warning "Expected at least two harness runs for fixture smoke, found $($runs.Count)"
        exit 1
    }
    $latestTwo = @($runs | Select-Object -First 2)
    $newer = $latestTwo[0]
    $older = $latestTwo[1]
    $jsonlOlder = Join-Path $older.FullName 'run.jsonl'
    $jsonlNewer = Join-Path $newer.FullName 'run.jsonl'
    if (-not (Test-Path $jsonlOlder)) {
        Write-Warning "No run.jsonl in $($older.FullName)"
        exit 1
    }
    if (-not (Test-Path $jsonlNewer)) {
        Write-Warning "No run.jsonl in $($newer.FullName)"
        exit 1
    }
    Write-Host "Fixture artifacts:" -ForegroundColor Cyan
    Write-Host "  first turn:  $jsonlOlder" -ForegroundColor DarkGray
    Write-Host "  second turn: $jsonlNewer" -ForegroundColor DarkGray
    & python (Join-Path $ProjectRoot 'tests\assert_harness_run.py') $jsonlOlder `
        --expect-tool editor_get_selection `
        --require-success
    $ac = $LASTEXITCODE
    if ($ac -ne 0) {
        exit $ac
    }
    & python (Join-Path $ProjectRoot 'tests\assert_harness_run.py') $jsonlNewer `
        --expect-tool scene_fuzzy_search `
        --require-success
    $ac = $LASTEXITCODE
    if ($ac -ne 0) {
        exit $ac
    }
}
Write-Host 'Headed scenario smoke finished.' -ForegroundColor Green
exit 0
