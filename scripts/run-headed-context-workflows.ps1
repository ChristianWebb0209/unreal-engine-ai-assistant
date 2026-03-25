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

function Escape-Win32QuotedArgContent {
    param([string]$Text)
    if ($null -eq $Text) { return '' }
    return $Text.Replace('"', '""')
}

function Invoke-HeadedEditor {
    param([string]$ExecCmds)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $EditorExe
    $psi.WorkingDirectory = $ProjectRoot
    $psi.UseShellExecute = $false
    $upEsc = Escape-Win32QuotedArgContent $UProject
    $ecEsc = Escape-Win32QuotedArgContent $ExecCmds
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
        Write-Host "  UnrealEditor exit: $($Proc.ExitCode)" -ForegroundColor $(if ($Proc.ExitCode -eq 0) { 'Green' } else { 'Yellow' })
        return $Proc.ExitCode
    }

    $run = {
        $proc = [System.Diagnostics.Process]::Start($psi)
        if (-not $proc) {
            Write-Error "Failed to start UnrealEditor: $EditorExe"
        }
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
        [string]$Mode
    )
    $env:UNREAL_AI_HARNESS_MESSAGE_FILE = $MsgAbs
    $env:UNREAL_AI_HARNESS_THREAD_GUID = $ThreadGuid
    Remove-Item Env:\UNREAL_AI_HARNESS_AGENT_MODE -ErrorAction SilentlyContinue
    $m = if ($Mode) { $Mode.ToLowerInvariant() } else { 'agent' }
    if ($m -ne 'agent') {
        $env:UNREAL_AI_HARNESS_AGENT_MODE = $m
    }
}

function Clear-ContextHarnessStepEnv {
    Remove-Item Env:\UNREAL_AI_HARNESS_MESSAGE_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:\UNREAL_AI_HARNESS_THREAD_GUID -ErrorAction SilentlyContinue
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
    Set-Content -LiteralPath (Join-Path $wfOut 'thread_id.txt') -Value $threadGuid -Encoding utf8

    Write-Host "Workflow $wfId thread=$threadGuid steps=$($steps.Count)" -ForegroundColor Cyan

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
            [void](Invoke-HeadedEditor $execCmds)
        }
        finally {
            Remove-Item Env:\UNREAL_AI_HARNESS_MAX_LLM_ROUNDS -ErrorAction SilentlyContinue
        }
    }
    else {
        $si3 = 0
        foreach ($st in $steps) {
            $si3++
            $msgAbs = (Resolve-Path -LiteralPath (Join-Path $job.BaseDir ([string]$st.message_file))).Path
            if (-not (Test-Path -LiteralPath $msgAbs)) {
                Write-Error "Message file not found: $msgAbs"
            }
            $mode = if ($st.mode) { [string]$st.mode } else { $defaultMode }
            Set-ContextHarnessStepEnv $msgAbs $threadGuid $mode
            if ($MaxLlmRounds -gt 0) {
                $env:UNREAL_AI_HARNESS_MAX_LLM_ROUNDS = [string]$MaxLlmRounds
            }
            try {
                Write-Host "  Step $($st.id): UNREAL_AI_HARNESS_MESSAGE_FILE=$msgAbs | UnrealAi.RunAgentTurn" -ForegroundColor Cyan
                [void](Invoke-HeadedEditor 'UnrealAi.RunAgentTurn,Quit')
            }
            finally {
                Clear-ContextHarnessStepEnv
            }
            $safeId = ([string]$st.id) -replace '[^a-zA-Z0-9_-]', '_'
            $stepDir = "step_{0:D2}_{1}" -f $si3, $safeId
            $dest = Join-Path $wfOut $stepDir
            [void](Copy-LatestHarnessToDir $runsRoot $dest)
        }
    }
}

Write-Host "Done. Bundle: python tests\bundle_context_workflow_review.py <tests\out\context_runs\...\workflow_folder>" -ForegroundColor Cyan
