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
    $env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'; .\scripts\run-headed-scenario-smoke.ps1
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = $(if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }),
    [string]$MatrixFilter = 'blueprint',
    [switch]$SkipCatalogMatrix
)

$ErrorActionPreference = 'Stop'
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

$MsgA = (Join-Path $ProjectRoot 'tests\harness_scenarios\user_scenario_a_selection.txt') -replace '\\', '/'
$MsgB = (Join-Path $ProjectRoot 'tests\harness_scenarios\user_scenario_b_scene_search.txt') -replace '\\', '/'
$FixtureRel = 'tests/harness_scenarios/fixture_two_agent_turns.json'

$env:UNREAL_AI_LLM_FIXTURE = $FixtureRel

$Parts = @()
if (-not $SkipCatalogMatrix) {
    if ([string]::IsNullOrWhiteSpace($MatrixFilter)) {
        $Parts += 'UnrealAi.RunCatalogMatrix'
    } else {
        $Parts += "UnrealAi.RunCatalogMatrix $MatrixFilter"
    }
}
# Quote paths: -ExecCmds splits on space; without quotes only UnrealAi.RunAgentTurn runs (no file arg).
$Parts += "UnrealAi.RunAgentTurn ""$MsgA"""
$Parts += "UnrealAi.RunAgentTurn ""$MsgB"""
$Parts += 'Quit'
$ExecCmds = ($Parts -join ';')

Write-Host "Headed scenario smoke: $ExecCmds" -ForegroundColor Cyan
Write-Host "UNREAL_AI_LLM_FIXTURE=$($env:UNREAL_AI_LLM_FIXTURE)" -ForegroundColor DarkGray

$ArgList = @(
    "`"$UProject`"",
    '-unattended',
    '-nop4',
    '-NoSplash',
    "-ExecCmds=$ExecCmds",
    '-log'
)

$p = Start-Process -FilePath $EditorExe -ArgumentList $ArgList -WorkingDirectory $ProjectRoot -PassThru -Wait
Write-Host "UnrealEditor exit code: $($p.ExitCode)" -ForegroundColor $(if ($p.ExitCode -eq 0) { 'Green' } else { 'Yellow' })

$runsRoot = Join-Path $ProjectRoot 'Saved\UnrealAiEditor\HarnessRuns'
if (-not (Test-Path $runsRoot)) {
    Write-Warning "No HarnessRuns folder at $runsRoot"
    exit 1
}
$latest = Get-ChildItem -Path $runsRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
if (-not $latest) {
    Write-Warning 'No harness run directories found.'
    exit 1
}
$jsonl = Join-Path $latest.FullName 'run.jsonl'
if (-not (Test-Path $jsonl)) {
    Write-Warning "No run.jsonl in $($latest.FullName)"
    exit 1
}

Write-Host "Asserting $jsonl" -ForegroundColor Cyan
& python (Join-Path $ProjectRoot 'tests\assert_harness_run.py') $jsonl `
    --expect-tool editor_get_selection `
    --expect-tool scene_fuzzy_search `
    --require-success
$ac = $LASTEXITCODE
if ($ac -ne 0) {
    exit $ac
}
Write-Host 'Headed scenario smoke OK.' -ForegroundColor Green
exit 0
