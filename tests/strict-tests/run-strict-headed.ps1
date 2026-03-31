#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Suite,
    [string]$EngineRoot = '',
    [string]$ProjectRoot = '',
    [int]$MaxSuites = 0,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

# Strict headed harness is implemented by reusing the qualitative headed executor,
# but with:
# - assertions supported via `assertions[]` in each turn
# - runs redirected into `tests/strict-tests/runs/`
#
# All output for this invocation goes under `tests/strict-tests/runs/run-<stamp>-<suite>/` (single top-level folder).
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$strictRootAbs = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$strictSuitesAbs = Join-Path $strictRootAbs 'suites'
$strictRunsRoot = Join-Path $strictRootAbs 'runs'
if (-not (Test-Path -LiteralPath $strictRunsRoot)) {
    New-Item -ItemType Directory -Force -Path $strictRunsRoot | Out-Null
}
$null = New-Item -ItemType Directory -Force -Path $strictSuitesAbs -ErrorAction SilentlyContinue

$suiteName = [string]$Suite
if ([string]::IsNullOrWhiteSpace($suiteName)) {
    throw 'Suite is required.'
}
if (-not $suiteName.ToLowerInvariant().EndsWith('.json')) {
    $suiteName = $suiteName + '.json'
}
$suitePath = Join-Path $strictSuitesAbs $suiteName
if (-not (Test-Path -LiteralPath $suitePath)) {
    throw "Suite not found: $suitePath"
}
$suiteFileName = [System.IO.Path]::GetFileName($suitePath)

$longRunner = Join-Path $repoRoot 'tests\qualitative-tests\run-qualitative-headed.ps1'

$runnerParams = @{
    ScenarioFolder = $strictSuitesAbs
    Suite = $suiteFileName
    EngineRoot = $EngineRoot
    ProjectRoot = $ProjectRoot
    MaxSuites = $MaxSuites
    RunsRoot = $strictRunsRoot
}
if ($DryRun) { $runnerParams.DryRun = $true }

# One PowerShell invocation => exactly one new directory under `$strictRunsRoot`
# (`run-<MM-dd-HH-mm>-<suiteSlug>` with optional numeric suffix). Per-suite and per-turn
# artifacts are nested under that folder via `tests/qualitative-tests/run-qualitative-headed.ps1`.
$code = 0
& $longRunner @runnerParams
$code = $LASTEXITCODE
exit $code
