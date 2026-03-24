#requires -Version 5.1
<#
  Run Unreal Editor automation tests for UnrealAiEditor (blank.uproject), capture logs + tool matrix JSON.

  Usage (from repo root):
    .\run-unreal-ai-tests.ps1
    .\run-unreal-ai-tests.ps1 -Build
    .\run-unreal-ai-tests.ps1 -TestFilter UnrealAiEditor -MatrixFilter blueprint
    $env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'; .\run-unreal-ai-tests.ps1

  UE 5.7: uses -ExecCmds="Automation RunTests <filter>; Quit" (validated pattern for Session Frontend tests).

  Close Unreal Editor before -Build if UnrealAiEditor.dll is locked.

  Outputs (for LLM iteration):
    tests\out\editor-last.log   — newest Saved\Logs\*.log after the run
    tests\out\last-matrix.json — copy of Saved\UnrealAiEditor\Automation\tool_matrix_last.json (if produced)
#>
[CmdletBinding()]
param(
    [switch]$Build,
    [string]$EngineRoot = $(if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }),
    [string]$TestFilter = 'UnrealAiEditor',
    [string]$MatrixFilter = '',
    [switch]$NullRhi,
    [string[]]$ExtraEditorArgs = @()
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'blank.uproject'
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$TestsOutDir = Join-Path $ProjectRoot 'tests\out'
$MatrixSaved = Join-Path $ProjectRoot 'Saved\UnrealAiEditor\Automation\tool_matrix_last.json'
$MatrixOut = Join-Path $TestsOutDir 'last-matrix.json'
$LogOut = Join-Path $TestsOutDir 'editor-last.log'

if (-not (Test-Path $UProject)) {
    Write-Error "Project file not found: $UProject"
}

if (-not (Test-Path $TestsOutDir)) {
    New-Item -ItemType Directory -Path $TestsOutDir -Force | Out-Null
}

if ($Build) {
    $BuildScript = Join-Path $ProjectRoot 'build-blank-editor.ps1'
    Write-Host "Building BlankEditor (headless)..." -ForegroundColor Cyan
    & $BuildScript -Headless -EngineRoot $EngineRoot
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if (-not (Test-Path $EditorCmd)) {
    Write-Error "UnrealEditor-Cmd.exe not found: $EditorCmd`nSet UE_ENGINE_ROOT or pass -EngineRoot."
}

try {
    $env:GIT_COMMIT = (git -C $ProjectRoot rev-parse HEAD 2>$null)
} catch {
    $env:GIT_COMMIT = ''
}

$ExecCmdsValue = "Automation RunTests $TestFilter; Quit"
$CmdArgs = @(
    "`"$UProject`"",
    '-unattended',
    '-nop4',
    '-nosplash',
    '-log',
    '-LogCmds=LogAutomationController Verbose, LogAutomationCommandLine Verbose'
)
if ($NullRhi) {
    $CmdArgs += '-nullrhi'
}
if ($MatrixFilter) {
    $CmdArgs += "-UnrealAiToolMatrixFilter=$MatrixFilter"
}
foreach ($x in $ExtraEditorArgs) {
    if (-not [string]::IsNullOrWhiteSpace($x)) {
        $CmdArgs += $x
    }
}
$CmdArgs += "-ExecCmds=$ExecCmdsValue"

Write-Host "Running: $EditorCmd" -ForegroundColor Cyan
Write-Host "  Args: $($CmdArgs -join ' ')" -ForegroundColor DarkGray

$logsDir = Join-Path $ProjectRoot 'Saved\Logs'

$p = Start-Process -FilePath $EditorCmd -ArgumentList $CmdArgs -WorkingDirectory $ProjectRoot -Wait -PassThru -NoNewWindow
$procExit = $p.ExitCode

# Copy newest log in Saved\Logs (typically the editor session that just finished)
$failLogParse = $false
try {
    if (Test-Path $logsDir) {
        $picked = Get-ChildItem -Path $logsDir -Filter '*.log' -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($picked) {
            Copy-Item -LiteralPath $picked.FullName -Destination $LogOut -Force
        }
    }
} catch {
    $failLogParse = $true
    Write-Warning "Could not copy editor log: $_"
}

$failCountFromLog = 0
$logText = ''
if (Test-Path $LogOut) {
    $logText = Get-Content -LiteralPath $LogOut -Raw -ErrorAction SilentlyContinue
    if ($logText) {
        $failCountFromLog += ([regex]::Matches($logText, 'Result=Fail')).Count
        if ($logText -match 'Failed\s+Test\s+Count\s*:\s*(\d+)') {
            $failCountFromLog = [Math]::Max($failCountFromLog, [int]$Matches[1])
        }
    }
}

if (Test-Path $MatrixSaved) {
    Copy-Item -LiteralPath $MatrixSaved -Destination $MatrixOut -Force
}

# --- Summary (stdout): easy for LLMs to grep ---
Write-Host ''
Write-Host '--- Unreal AI test run summary ---' -ForegroundColor Cyan
Write-Host "ProcessExitCode: $procExit"
Write-Host "Log:             $LogOut"
Write-Host "Matrix JSON:     $MatrixOut"
Write-Host "Result=Fail hits in log: $failCountFromLog"
if ($failLogParse) {
    Write-Host 'Log copy warning: see stderr above' -ForegroundColor Yellow
}

$contractViolations = $null
if (Test-Path $MatrixOut) {
    try {
        $j = Get-Content -LiteralPath $MatrixOut -Raw | ConvertFrom-Json
        $contractViolations = $j.summary.contract_violations
        Write-Host "Matrix summary.contract_violations: $contractViolations"
    } catch {
        Write-Host 'Matrix JSON: could not parse summary (file still copied)' -ForegroundColor Yellow
    }
} else {
    Write-Host 'Matrix JSON: not found (CatalogMatrix test may not have run or failed before write)' -ForegroundColor Yellow
}

$finalExit = 0
if ($procExit -ne 0) {
    $finalExit = $procExit
} elseif ($failCountFromLog -gt 0) {
    $finalExit = 1
} elseif ($null -ne $contractViolations -and [int]$contractViolations -gt 0) {
    $finalExit = 1
}

Write-Host "FinalExitCode: $finalExit" -ForegroundColor $(if ($finalExit -eq 0) { 'Green' } else { 'Red' })
exit $finalExit
