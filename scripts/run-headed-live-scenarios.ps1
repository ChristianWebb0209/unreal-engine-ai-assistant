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

  See docs\LIVE_HARNESS.md and docs\AGENT_HARNESS_HANDOFF.md.
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = $(if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }),
    [string]$ProjectRoot = '',
    [string]$Manifest = '',
    [int]$MaxScenarios = 0,
    [switch]$SkipMatrix,
    [string]$MatrixFilter = '',
    [switch]$SingleSession,
    [switch]$DumpContext,
    [switch]$DryRun,
    [switch]$AllowFixture
)

$ErrorActionPreference = 'Stop'

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

if ($env:UNREAL_AI_LLM_FIXTURE -and -not $AllowFixture) {
    Write-Error "UNREAL_AI_LLM_FIXTURE is set ($($env:UNREAL_AI_LLM_FIXTURE)). Clear it for live API runs or pass -AllowFixture to override."
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
    # UE -ExecCmds splits on spaces; the message path must be quoted so it stays one console arg.
    # Use PowerShell doubled quotes ("") inside a double-quoted string — backtick-escaped quotes were
    # lost when Start-Process built the argv, producing Cmd: UnrealAi.RunAgentTurn with no file.
    $p = ($MsgAbs -replace '\\', '/')
    $m = if ($Mode) { $Mode.ToLowerInvariant() } else { 'agent' }
    if ($m -eq 'agent') {
        return "UnrealAi.RunAgentTurn ""$p"""
    }
    $g = [guid]::NewGuid().ToString()
    return "UnrealAi.RunAgentTurn ""$p"" $g $m"
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

function Invoke-HeadedEditor {
    param([string]$ExecCmds)
    $ArgList = @(
        "`"$UProject`"",
        '-unattended',
        '-nop4',
        '-NoSplash',
        "-ExecCmds=$ExecCmds",
        '-log'
    )
    if ($DumpContext) {
        $prevDump = $env:UNREAL_AI_HARNESS_DUMP_CONTEXT
        $env:UNREAL_AI_HARNESS_DUMP_CONTEXT = '1'
        try {
            $p = Start-Process -FilePath $EditorExe -ArgumentList $ArgList -WorkingDirectory $ProjectRoot -PassThru -Wait
            Write-Host "UnrealEditor exit code: $($p.ExitCode)" -ForegroundColor $(if ($p.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
            return $p.ExitCode
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
        $p = Start-Process -FilePath $EditorExe -ArgumentList $ArgList -WorkingDirectory $ProjectRoot -PassThru -Wait
        Write-Host "UnrealEditor exit code: $($p.ExitCode)" -ForegroundColor $(if ($p.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
        return $p.ExitCode
    }
}

if (-not $SkipMatrix) {
    if ([string]::IsNullOrWhiteSpace($MatrixFilter)) {
        $matrixCmd = 'UnrealAi.RunCatalogMatrix'
    }
    else {
        $matrixCmd = "UnrealAi.RunCatalogMatrix $MatrixFilter"
    }
    $matrixExec = "$matrixCmd;Quit"
    Write-Host "Catalog matrix (headed): $matrixExec" -ForegroundColor Cyan
    [void](Invoke-HeadedEditor $matrixExec)
}

if ($SingleSession) {
    $namesBefore = @{}
    if (Test-Path $runsRoot) {
        foreach ($d in Get-ChildItem -Path $runsRoot -Directory -ErrorAction SilentlyContinue) {
            $namesBefore[$d.Name] = $true
        }
    }
    $parts = @()
    $si = 0
    foreach ($s in $scenarios) {
        $si++
        $msgAbs = Join-Path $manifestDir ([string]$s.message_file)
        if (-not (Test-Path -LiteralPath $msgAbs)) {
            Write-Error "Message file not found: $msgAbs"
        }
        $mode = if ($s.mode) { [string]$s.mode } else { 'agent' }
        $parts += (Build-RunAgentTurnExec $msgAbs $mode)
    }
    $parts += 'Quit'
    $ExecCmds = $parts -join ';'
    Write-Host "Single-session ExecCmds (first 200 chars): $($ExecCmds.Substring(0, [Math]::Min(200, $ExecCmds.Length)))..." -ForegroundColor Cyan
    [void](Invoke-HeadedEditor $ExecCmds)

    if (-not (Test-Path $runsRoot)) {
        Write-Warning "No HarnessRuns at $runsRoot"
        exit 1
    }
    $newDirs = @(Get-ChildItem -Path $runsRoot -Directory | Where-Object { -not $namesBefore.ContainsKey($_.Name) } | Sort-Object Name)
    $idx = 0
    foreach ($s in $scenarios) {
        if ($idx -ge $newDirs.Count) {
            Write-Warning "Missing harness output for scenario $($s.id) (only $($newDirs.Count) new dirs)"
            break
        }
        $dest = Join-Path $outRoot ([string]$s.id)
        $src = $newDirs[$idx].FullName
        if (Test-Path $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
        Copy-Item -LiteralPath $src -Destination $dest -Recurse
        Write-Host "Copied $($newDirs[$idx].Name) -> $dest" -ForegroundColor Green
        $idx++
    }
    exit 0
}

foreach ($s in $scenarios) {
    $msgAbs = Join-Path $manifestDir ([string]$s.message_file)
    if (-not (Test-Path -LiteralPath $msgAbs)) {
        Write-Error "Message file not found: $msgAbs"
    }
    $mode = if ($s.mode) { [string]$s.mode } else { 'agent' }
    $one = "$(Build-RunAgentTurnExec $msgAbs $mode);Quit"
    Write-Host "Scenario $($s.id): $one" -ForegroundColor Cyan
    [void](Invoke-HeadedEditor $one)
    $dest = Join-Path $outRoot ([string]$s.id)
    [void](Copy-LatestHarnessToScenario $runsRoot $dest)
}

Write-Host "Done. Review bundle: python tests\bundle_live_harness_review.py `"$outRoot`"" -ForegroundColor Cyan
