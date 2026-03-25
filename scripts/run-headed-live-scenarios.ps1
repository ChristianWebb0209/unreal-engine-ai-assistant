#requires -Version 5.1
<#
  Live headed qualitative tier: launch headed Unreal Editor and run manifest-driven UnrealAi.RunAgentTurn
  (real API — do NOT set UNREAL_AI_LLM_FIXTURE). Copies each harness output to tests/out/live_runs/<suite>/<scenario_id>/.

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
    [switch]$AllowFixture,
    [int]$MaxLlmRounds = 4,
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

if (-not $AllowFixture) {
    if ($env:UNREAL_AI_LLM_FIXTURE) {
        Write-Warning "UNREAL_AI_LLM_FIXTURE was set ($($env:UNREAL_AI_LLM_FIXTURE)); clearing for live API run (use -AllowFixture to keep fixture)."
        Remove-Item Env:\UNREAL_AI_LLM_FIXTURE -ErrorAction SilentlyContinue
    }
}
elseif ($env:UNREAL_AI_LLM_FIXTURE) {
    Write-Host "Using UNREAL_AI_LLM_FIXTURE=$($env:UNREAL_AI_LLM_FIXTURE)" -ForegroundColor DarkGray
}
else {
    Write-Warning "-AllowFixture was passed but UNREAL_AI_LLM_FIXTURE is not set."
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
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

function Get-HarnessRunDirs {
    param([string]$RunsRoot)
    if (-not (Test-Path $RunsRoot)) { return @() }
    Get-ChildItem -Path $RunsRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending
}

function Copy-LatestHarnessToScenario {
    param(
        [string]$RunsRoot,
        [string]$DestDir
    )
    $latest = Get-HarnessRunDirs $RunsRoot | Select-Object -First 1
    if (-not $latest) {
        Write-Warning "No harness run directory under $RunsRoot"
        return $false
    }
    if (Test-Path $DestDir) {
        Remove-Item -LiteralPath $DestDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DestDir) | Out-Null
    Copy-Item -LiteralPath $latest.FullName -Destination $DestDir -Recurse
    Write-Host "Copied harness run $($latest.Name) -> $DestDir" -ForegroundColor Green
    return $true
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

function Escape-Win32QuotedArgContent {
    param([string]$Text)
    if ($null -eq $Text) { return '' }
    return $Text.Replace('"', '""')
}

function Invoke-HeadedEditor {
    param([string]$ExecCmds)
    $upEsc = Escape-Win32QuotedArgContent $UProject
    $ecEsc = Escape-Win32QuotedArgContent $ExecCmds
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $EditorExe
    $psi.WorkingDirectory = $ProjectRoot
    $psi.UseShellExecute = $false
    $psi.Arguments = '"' + $upEsc + '" -unattended -nop4 -NoSplash -ExecCmds="' + $ecEsc + '" -log'

    function Wait-ForEditorExitOrKill {
        param([System.Diagnostics.Process]$Proc)
        if ($null -eq $Proc) { return 1 }
        $waitMs = [Math]::Max(1000, $EditorExitTimeoutSec * 1000)
        if (-not $Proc.WaitForExit($waitMs)) {
            Write-Warning "UnrealEditor did not exit within $EditorExitTimeoutSec seconds; force-killing for fast iteration."
            try { $Proc.Kill() } catch {}
            try { $Proc.WaitForExit() } catch {}
        }
        Write-Host "UnrealEditor exit code: $($Proc.ExitCode)" -ForegroundColor $(if ($Proc.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
        return $Proc.ExitCode
    }

    if ($DumpContext) {
        $prevDump = $env:UNREAL_AI_HARNESS_DUMP_CONTEXT
        $env:UNREAL_AI_HARNESS_DUMP_CONTEXT = '1'
        try {
            $p = [System.Diagnostics.Process]::Start($psi)
            return (Wait-ForEditorExitOrKill $p)
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
        $p = [System.Diagnostics.Process]::Start($psi)
        return (Wait-ForEditorExitOrKill $p)
    }
}

if (-not $SkipMatrix) {
    if ([string]::IsNullOrWhiteSpace($MatrixFilter)) {
        $matrixCmd = 'UnrealAi.RunCatalogMatrix'
    }
    else {
        $matrixCmd = "UnrealAi.RunCatalogMatrix $MatrixFilter"
    }
    $matrixExec = "$matrixCmd,Quit"
    Write-Host "Catalog matrix (headed): $matrixExec" -ForegroundColor Cyan
    [void](Invoke-HeadedEditor $matrixExec)
}

if ($SingleSession) {
    Write-Warning 'SingleSession is unsupported when each scenario uses UNREAL_AI_HARNESS_MESSAGE_FILE (env cannot change mid-ExecCmds). Running one editor launch per scenario.'
}

foreach ($s in $scenarios) {
    $msgAbs = Join-Path $manifestDir ([string]$s.message_file)
    if (-not (Test-Path -LiteralPath $msgAbs)) {
        Write-Error "Message file not found: $msgAbs"
    }
    $mode = if ($s.mode) { [string]$s.mode } else { 'agent' }
    $env:UNREAL_AI_HARNESS_MESSAGE_FILE = $msgAbs
    if ($MaxLlmRounds -gt 0) {
        $env:UNREAL_AI_HARNESS_MAX_LLM_ROUNDS = [string]$MaxLlmRounds
    }
    try {
        $one = "$(Build-RunAgentTurnExec $msgAbs $mode),Quit"
        Write-Host "Scenario $($s.id): UNREAL_AI_HARNESS_MESSAGE_FILE=$msgAbs | $one" -ForegroundColor Cyan
        [void](Invoke-HeadedEditor $one)
    }
    finally {
        Remove-Item Env:\UNREAL_AI_HARNESS_MESSAGE_FILE -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_MAX_LLM_ROUNDS -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_THREAD_GUID -ErrorAction SilentlyContinue
        Remove-Item Env:\UNREAL_AI_HARNESS_AGENT_MODE -ErrorAction SilentlyContinue
    }
    $dest = Join-Path $outRoot ([string]$s.id)
    [void](Copy-LatestHarnessToScenario $runsRoot $dest)
}

Write-Host "Done. Review bundle: python tests\bundle_live_harness_review.py `"$outRoot`"" -ForegroundColor Cyan
