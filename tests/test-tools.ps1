#requires -Version 5.1
<#
  Tool catalog checks (tracked; run manually or from CI).

  Default: Python static routing + optional --llm against plugin_settings.json.
  -UseUnrealAutomation: Unreal Editor automation test (HeadlessPromptToolRouting).

  Usage:
    .\test-tools.ps1
    .\test-tools.ps1 -Llm -LlmMax 82
    .\test-tools.ps1 -UseUnrealAutomation
    .\test-tools.ps1 -UseUnrealAutomation -Headed

  -Headed: use UnrealEditor.exe (visible window) instead of UnrealEditor-Cmd.exe.

  -AllowDialogs: omit -unattended (blocking dialogs may appear; automation can hang).
#>
[CmdletBinding()]
param(
    [switch]$Llm,
    [int]$LlmMax = 5,
    [switch]$UseUnrealAutomation,
    [string]$EngineRoot = $(if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }),
    [string]$TestFilter = 'UnrealAiEditor.Tools.HeadlessPromptToolRouting',
    [switch]$NullRhi,
    [switch]$Headed,
    [switch]$AllowDialogs,
    [string[]]$ExtraEditorArgs = @()
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'blank.uproject'
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$CatalogPath = Join-Path $ProjectRoot 'Plugins\UnrealAiEditor\Resources\UnrealAiToolCatalog.json'
$FixtureDir = Join-Path $ProjectRoot 'tests'
$FixturePath = Join-Path $FixtureDir 'tool-call-prompts.generated.json'
$OutDir = Join-Path $ProjectRoot 'tests\out'
$LogOut = Join-Path $OutDir 'test-tools-editor-last.log'
$PyCheck = Join-Path $ProjectRoot 'tests\tool_catalog_routing_check.py'

if (-not (Test-Path $CatalogPath)) { throw "Tool catalog not found: $CatalogPath" }
if (-not (Test-Path $FixtureDir)) { New-Item -ItemType Directory -Path $FixtureDir -Force | Out-Null }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$catalog = Get-Content -LiteralPath $CatalogPath -Raw | ConvertFrom-Json
$tools = @($catalog.tools | Where-Object { $_.tool_id } | Sort-Object tool_id)
if ($tools.Count -eq 0) { throw "No tools found in catalog." }

$cases = @()
$n = 0
foreach ($t in $tools) {
    $n++
    $id = [string]$t.tool_id
    $summary = [string]$t.summary
    if ([string]::IsNullOrWhiteSpace($summary)) { $summary = "run the requested action" }
    $mode = 'agent'
    if ($t.modes.agent -eq $true -or $t.modes.fast -eq $true) {
        $mode = 'agent'
    } elseif ($t.modes.orchestrate -eq $true) {
        $mode = 'orchestrate'
    } elseif ($t.modes.ask -eq $true) {
        $mode = 'ask'
    } else {
        continue
    }
    if ($n -eq 1) {
        $prompt = "Hey, let's start a continuous tools QA chat. First, please call tool '$id' and use it to $summary."
    } else {
        $prompt = "Great, continuing the same thread: now call tool '$id' and proceed naturally."
    }
    $cases += [pscustomobject]@{
        prompt = $prompt
        mode = $mode
        expected_tool_calls = @($id)
    }
}

$fixtureObj = [pscustomobject]@{
    generated_at = (Get-Date).ToString('o')
    source_catalog = $CatalogPath
    case_count = $cases.Count
    notes = "Auto-generated; one prompt per tool_id."
    cases = $cases
}
$fixtureObj | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $FixturePath -Encoding UTF8

Write-Host "Fixture: $FixturePath ($($cases.Count) cases)" -ForegroundColor Cyan

if (-not $UseUnrealAutomation) {
    if (-not (Test-Path -LiteralPath $PyCheck)) { throw "Missing script: $PyCheck" }
    if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        throw "Python 3 is not on PATH. Install Python or run with -UseUnrealAutomation."
    }
    Write-Host "Running: $PyCheck" -ForegroundColor Cyan
    $pyArgs = @($PyCheck, '--catalog', $CatalogPath, '--fixture', $FixturePath)
    if ($Llm) {
        $auditLog = Join-Path $OutDir ("llm-audit-{0:yyyyMMdd-HHmmss}.log" -f (Get-Date))
        $pyArgs += @('--llm', '--llm-max', "$LlmMax", '--audit-log', $auditLog)
    }
    & python @pyArgs
    $pyExit = $LASTEXITCODE
    Write-Host ''
    Write-Host '--- test-tools summary ---' -ForegroundColor Cyan
    Write-Host "FinalExitCode:   $pyExit" -ForegroundColor $(if ($pyExit -eq 0) { 'Green' } else { 'Red' })
    exit $pyExit
}

if (-not (Test-Path $UProject)) { throw "Project file not found: $UProject" }
$EditorBinary = if ($Headed) { $EditorExe } else { $EditorCmd }
if (-not (Test-Path $EditorBinary)) {
    $exeName = if ($Headed) { 'UnrealEditor.exe' } else { 'UnrealEditor-Cmd.exe' }
    throw "$exeName not found: $EditorBinary"
}

$env:UNREAL_AI_TOOL_PROMPTS_FIXTURE = $FixturePath
$ExecCmdsValue = "Automation RunTests $TestFilter; Quit"
$CmdArgs = @("`"$UProject`"")
if (-not $AllowDialogs) {
    $CmdArgs += '-unattended'
}
$CmdArgs += @(
    '-nop4',
    '-nosplash',
    '-log',
    '-LogCmds=LogAutomationController Verbose, LogAutomationCommandLine Verbose',
    "-ExecCmds=$ExecCmdsValue"
)
if ($NullRhi) { $CmdArgs += '-nullrhi' }
foreach ($x in $ExtraEditorArgs) {
    if (-not [string]::IsNullOrWhiteSpace($x)) { $CmdArgs += $x }
}
Write-Host "Running: $EditorBinary" -ForegroundColor Cyan
if ($Headed) {
    Write-Host "  Mode: headed (visible editor window)" -ForegroundColor DarkGray
}
$logsDir = Join-Path $ProjectRoot 'Saved\Logs'
$p = Start-Process -FilePath $EditorBinary -ArgumentList $CmdArgs -WorkingDirectory $ProjectRoot -Wait -PassThru -NoNewWindow
$procExit = $p.ExitCode
$blankLog = Join-Path $logsDir 'blank.log'
if (Test-Path -LiteralPath $blankLog) {
    Copy-Item -LiteralPath $blankLog -Destination $LogOut -Force
}
$failCountFromLog = 0
if (Test-Path $LogOut) {
    $logText = Get-Content -LiteralPath $LogOut -Raw -ErrorAction SilentlyContinue
    if ($logText) {
        $failCountFromLog = ([regex]::Matches($logText, 'Result=Fail')).Count
        if ($logText -match 'Failed\s+Test\s+Count\s*:\s*(\d+)') {
            $failCountFromLog = [Math]::Max($failCountFromLog, [int]$Matches[1])
        }
    }
}
$finalExit = if ($procExit -ne 0) { $procExit } elseif ($failCountFromLog -gt 0) { 1 } else { 0 }
Write-Host "FinalExitCode:   $finalExit" -ForegroundColor $(if ($finalExit -eq 0) { 'Green' } else { 'Red' })
exit $finalExit
