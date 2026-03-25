#requires -Version 5.1
<#
  Context manager qualitative tier: headed Unreal Editor, multi-turn workflows with a STABLE thread id
  per workflow so context persists like Agent Chat. Copies each harness run to
  tests/out/context_runs/<suite_id>/<workflow_id>/step_<nn>_<step_id>/.

  Use -SuiteManifest tests\context_workflows\suite.json or -WorkflowManifest for one workflow.
  Set -DumpContext or UNREAL_AI_HARNESS_DUMP_CONTEXT=1 for context_window_*.txt dumps.

  Do NOT set UNREAL_AI_LLM_FIXTURE for realistic runs (use -AllowFixture to override).

  See docs\CONTEXT_HARNESS.md and tests\context_workflows\README.md
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = $(if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }),
    [string]$ProjectRoot = '',
    [string]$SuiteManifest = '',
    [string]$WorkflowManifest = '',
    [int]$MaxWorkflows = 0,
    [int]$MaxSteps = 0,
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

$UProject = Join-Path $ProjectRoot 'blank.uproject'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $EditorExe)) {
    Write-Error "UnrealEditor.exe not found: $EditorExe (set UE_ENGINE_ROOT or -EngineRoot)"
}

if ($env:UNREAL_AI_LLM_FIXTURE -and -not $AllowFixture) {
    Write-Error "UNREAL_AI_LLM_FIXTURE is set ($($env:UNREAL_AI_LLM_FIXTURE)). Clear it for live API runs or pass -AllowFixture."
}

if ($WorkflowManifest -and $SuiteManifest) {
    Write-Error "Specify only one of -WorkflowManifest or -SuiteManifest."
}
if (-not $WorkflowManifest -and -not $SuiteManifest) {
    $SuiteManifest = Join-Path $ProjectRoot 'tests\context_workflows\suite.json'
}

$runsRoot = Join-Path $ProjectRoot 'Saved\UnrealAiEditor\HarnessRuns'

function Get-HarnessRunDirs {
    param([string]$Root)
    if (-not (Test-Path $Root)) { return @() }
    Get-ChildItem -Path $Root -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending
}

function Copy-LatestHarnessToDir {
    param([string]$Root, [string]$DestDir)
    $latest = Get-HarnessRunDirs $Root | Select-Object -First 1
    if (-not $latest) {
        Write-Warning "No harness run under $Root"
        return $false
    }
    if (Test-Path $DestDir) {
        Remove-Item -LiteralPath $DestDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DestDir) | Out-Null
    Copy-Item -LiteralPath $latest.FullName -Destination $DestDir -Recurse
    Write-Host "  Copied $($latest.Name) -> $DestDir" -ForegroundColor Green
    return $true
}

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
            Write-Host "  UnrealEditor exit: $($p.ExitCode)" -ForegroundColor $(if ($p.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
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
        Write-Host "  UnrealEditor exit: $($p.ExitCode)" -ForegroundColor $(if ($p.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
        return $p.ExitCode
    }
}

function Build-RunAgentTurnWithThread {
    param(
        [string]$MsgAbs,
        [string]$ThreadGuid,
        [string]$Mode
    )
    $p = ($MsgAbs -replace '\\', '/')
    $m = if ($Mode) { $Mode.ToLowerInvariant() } else { 'agent' }
    return "UnrealAi.RunAgentTurn ""$p"" $ThreadGuid $m"
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
    exit 0
}

if (-not $SkipMatrix) {
    if ([string]::IsNullOrWhiteSpace($MatrixFilter)) {
        $matrixCmd = 'UnrealAi.RunCatalogMatrix'
    }
    else {
        $matrixCmd = "UnrealAi.RunCatalogMatrix $MatrixFilter"
    }
    Write-Host "Catalog matrix (headed): $matrixCmd;Quit" -ForegroundColor Cyan
    [void](Invoke-HeadedEditor "$matrixCmd;Quit")
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
    Set-Content -LiteralPath (Join-Path $wfOut 'thread_id.txt') -Value $threadGuid -Encoding utf8

    Write-Host "Workflow $wfId thread=$threadGuid steps=$($steps.Count)" -ForegroundColor Cyan

    if ($SingleSession) {
        $namesBefore = @{}
        if (Test-Path $runsRoot) {
            foreach ($d in Get-ChildItem -Path $runsRoot -Directory -ErrorAction SilentlyContinue) {
                $namesBefore[$d.Name] = $true
            }
        }
        $parts = @()
        $ix = 0
        foreach ($st in $steps) {
            $ix++
            $msgAbs = Join-Path $job.BaseDir ([string]$st.message_file)
            if (-not (Test-Path -LiteralPath $msgAbs)) {
                Write-Error "Message file not found: $msgAbs"
            }
            $mode = if ($st.mode) { [string]$st.mode } else { $defaultMode }
            $parts += (Build-RunAgentTurnWithThread $msgAbs $threadGuid $mode)
        }
        $parts += 'Quit'
        $ExecCmds = $parts -join ';'
        Write-Host "  SingleSession (len=$($ExecCmds.Length))" -ForegroundColor DarkGray
        [void](Invoke-HeadedEditor $ExecCmds)

        if (-not (Test-Path $runsRoot)) {
            Write-Warning "No HarnessRuns at $runsRoot"
            continue
        }
        $newDirs = @(Get-ChildItem -Path $runsRoot -Directory | Where-Object { -not $namesBefore.ContainsKey($_.Name) } | Sort-Object Name)
        $idx = 0
        $si2 = 0
        foreach ($st in $steps) {
            $si2++
            if ($idx -ge $newDirs.Count) {
                Write-Warning "Missing harness dir for step $($st.id)"
                break
            }
            $safeId = ([string]$st.id) -replace '[^a-zA-Z0-9_-]', '_'
            $stepDir = "step_{0:D2}_{1}" -f $si2, $safeId
            $dest = Join-Path $wfOut $stepDir
            if (Test-Path $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
            Copy-Item -LiteralPath $newDirs[$idx].FullName -Destination $dest -Recurse
            Write-Host "  Copied $($newDirs[$idx].Name) -> $dest" -ForegroundColor Green
            $idx++
        }
    }
    else {
        $si3 = 0
        foreach ($st in $steps) {
            $si3++
            $msgAbs = Join-Path $job.BaseDir ([string]$st.message_file)
            if (-not (Test-Path -LiteralPath $msgAbs)) {
                Write-Error "Message file not found: $msgAbs"
            }
            $mode = if ($st.mode) { [string]$st.mode } else { $defaultMode }
            $one = "$(Build-RunAgentTurnWithThread $msgAbs $threadGuid $mode);Quit"
            Write-Host "  Step $($st.id): $one" -ForegroundColor Cyan
            [void](Invoke-HeadedEditor $one)
            $safeId = ([string]$st.id) -replace '[^a-zA-Z0-9_-]', '_'
            $stepDir = "step_{0:D2}_{1}" -f $si3, $safeId
            $dest = Join-Path $wfOut $stepDir
            [void](Copy-LatestHarnessToDir $runsRoot $dest)
        }
    }
}

Write-Host "Done. Bundle: python tests\bundle_context_workflow_review.py <tests\out\context_runs\...\workflow_folder>" -ForegroundColor Cyan
