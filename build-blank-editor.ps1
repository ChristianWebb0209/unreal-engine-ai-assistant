#requires -Version 5.1
<#
  Build the BlankEditor target (includes UnrealAiEditor plugin), then launch the editor
  unless -Headless or --headless is set.

  PowerShell requires & to invoke .bat files with arguments — a bare quoted path is not a command.

  Usage (from repo root):
    .\build-blank-editor.ps1
    .\build-blank-editor.ps1 -Headless
    .\build-blank-editor.ps1 --headless
    .\build-blank-editor.ps1 -AutomationTests
    .\build-blank-editor.ps1 -AutomationTests -Headless
    .\build-blank-editor.ps1 -GenerateProjectFiles
    $env:UE_ENGINE_ROOT = 'D:\Epic\UE_5.7'; .\build-blank-editor.ps1

  If execution is blocked:
    powershell -ExecutionPolicy Bypass -File .\build-blank-editor.ps1

  If linking fails with LNK1104 on UnrealEditor-UnrealAiEditor.dll, close Unreal Editor (it loads the plugin)
  and run again. -NoHotReloadFromIDE avoids the Live Coding mutex but cannot override a loaded DLL lock.
#>
[CmdletBinding()]
param(
    [switch]$GenerateProjectFiles,
    [string]$EngineRoot = $(if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }),
    [switch]$Headless,
    [switch]$AutomationTests,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArguments
)

foreach ($r in $RemainingArguments) {
    if ($r -eq '--headless' -or $r -eq '-headless') {
        $Headless = $true
    }
    if ($r -eq '--AutomationTests' -or $r -eq '-AutomationTests') {
        $AutomationTests = $true
    }
}

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$UProject = Join-Path $ProjectRoot 'blank.uproject'
$BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$EditorCmdExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

if (-not (Test-Path $UProject)) {
    Write-Error "Project file not found: $UProject"
}
if (-not (Test-Path $BuildBat)) {
    Write-Error "Unreal Build.bat not found: $BuildBat`nSet UE_ENGINE_ROOT or pass -EngineRoot."
}

if ($GenerateProjectFiles) {
    Write-Host "Generating Visual Studio project files..." -ForegroundColor Cyan
    & $BuildBat -projectfiles "-project=$UProject" -game -rocket -progress
    exit $LASTEXITCODE
}

Write-Host "Building BlankEditor (Development | Win64)..." -ForegroundColor Cyan
& $BuildBat BlankEditor Win64 Development "-project=$UProject" -waitmutex -NoHotReloadFromIDE
$BuildExit = $LASTEXITCODE
if ($BuildExit -ne 0) {
    exit $BuildExit
}

if ($AutomationTests) {
    if (-not (Test-Path $EditorCmdExe)) {
        Write-Error "UnrealEditor-Cmd.exe not found: $EditorCmdExe`nSet UE_ENGINE_ROOT or pass -EngineRoot."
    }
    Write-Host "Running UnrealAiEditor.Tools automation (headless)..." -ForegroundColor Cyan
    & $EditorCmdExe $UProject -unattended -nop4 -NoSplash -nullrhi -nosound `
        -ExecCmds="Automation RunTests UnrealAiEditor.Tools;Quit" `
        -log
    $TestExit = $LASTEXITCODE
    if ($TestExit -ne 0) {
        Write-Error "Automation tests failed (exit $TestExit)."
        exit $TestExit
    }
    Write-Host "Automation tests passed." -ForegroundColor Green
}

if (-not $Headless) {
    if (-not (Test-Path $EditorExe)) {
        Write-Error "UnrealEditor.exe not found: $EditorExe`nSet UE_ENGINE_ROOT or pass -EngineRoot."
    }
    Write-Host "Launching Unreal Editor..." -ForegroundColor Cyan
    Start-Process -FilePath $EditorExe -ArgumentList "`"$UProject`"" -WorkingDirectory $ProjectRoot
}

exit 0
