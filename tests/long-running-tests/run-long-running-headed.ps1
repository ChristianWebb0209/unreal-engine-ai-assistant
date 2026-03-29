#requires -Version 5.1
<#
  Long-running headed harness runner for bulk conversational suites.

  Purpose
  - Run many multi-turn conversations in headed Unreal Editor using UnrealAi.RunAgentTurn.
  - Recursively finds suite JSON files (default name: suite.json) under -ScenarioFolder.
  - Runs exactly ONE editor process for the whole batch: all suites execute in one ExecCmds chain (sequential).
  - Each suite gets a fresh thread id; before the next suite, UnrealAi.ForgetThread removes the previous thread.
  - The batch fails (nonzero exit) if the editor exits nonzero OR any turn does not reach run_finished in run.jsonl.

  Windows command-line limit
  - A very long -ExecCmds string can exceed CreateProcess limits; paths are shortened (8.3) where possible.
  - If the combined command is still over -MaxExecCmdChars, the script falls back to one editor launch per suite
    (same strict per-suite completion checks). Use -ForceSingleSession to error instead of falling back.

  Usage (from repo root):
    .\tests\long-running-tests\run-long-running-headed.ps1 -ScenarioFolder tests\long-running-tests
    .\tests\long-running-tests\run-long-running-headed.ps1 -ScenarioFolder fine-tune-01-tool-definitions
    .\tests\long-running-tests\run-long-running-headed.ps1 -ScenarioFolder my-suite -DryRun
  Bandwidth: -MaxSuites N runs only the first N suite files (recursive sort order). Use 0 for all (default).
    N may be negative (e.g. -MaxSuites -1 = first suite only, -2 = first two). Same as positive |N|.
  Budget (harness): primary stop = 4 consecutive identical tool failures OR per-turn token cap (model profile / default 500k);
    round cap is a 512 backstop. -MaxLlmRounds / -MaxTurnTokens are recorded in summary.json only; tune limits in the model profile JSON (env overrides removed).

  JSON schema (suite files):
  {
    "suite_id": "optional_suite_name",
    "default_type": "agent",
    "turns": [
      { "type": "ask",   "request": "What is currently selected?" },
      { "type": "agent", "request": "Open the selected blueprint and inspect it." },
      { "type": "plan",  "request": "Plan a refactor in small steps." }
    ]
  }

  Notes
  - "type" values allowed: ask, agent, plan.
  - "plan" is mapped to harness mode "plan".
  - Output layout (each script invocation gets one new folder under the central runs directory; older batches are kept):
      tests\long-running-tests\runs\run-<index>-<localMachineTimestamp>\
        Index is a monotonic integer across all scenarios. Timestamp uses local machine time with ms resolution.
        - last-suite-summary.json  (batch metadata; only under this run folder)
        - editor_console_saved.log  (single-session mode: full Unreal Saved/Logs copy for the whole batch)
        <path-slug>\
          - summary.json
          - workflow-input.json (copy of source)
          - thread_id.txt
          - editor_console_saved.log (or _chunkNN): Unreal project log after that suite's editor session(s)
          - editor_console_stdout.txt / editor_console_stderr.txt (process streams; UE_LOG harness output is primarily in Unreal's -log window + Saved/Logs, not stdout)
          - turn_messages\turn_XX.txt
          - turns\step_XX\run.jsonl (+ context dumps)
          - turns\step_XX\context_decision_logs\*.jsonl
        In single-session mode, the same full-session log is also copied to each suite folder as editor_console_full_batch.log

  Context observability is controlled in C++ (UnrealAiRuntimeDefaults.h). -ReduceContextNoise passes nodump as the 5th RunAgentTurn arg for quieter batches.

  Interactive viewing (headed runs): use the main Unreal Editor window (large viewport/tabs). The script does not steal focus (-BringEditorToForeground to opt in). -log opens a separate log console — that is not the level viewport.

  Logging (defaults — no extra flags)
  - By default: process stdout/stderr are captured to editor_console_stdout/stderr .txt under the run folder; harness/UE_LOG
    output is shown in the Unreal -log console window and appended to Saved/Logs/<project>.log (not streamed into PowerShell).

  -LiveProjectLog: also tail Saved/Logs/<project>.log into this PowerShell window while the editor runs (useful in Cursor).
    Long batches also print periodic [harness] lines (turns M/N, elapsed wall time, ETA after the first turn completes).

  -NoLiveProjectLog: force-disable host tail (default is already off; use if you pass -LiveProjectLog elsewhere and want off).

  -MirrorEditorProcessStreamsToHost: also echo the editor process stdout/stderr lines to this PowerShell (in addition to tee files). Default is off; Unreal on Windows rarely mirrors UE_LOG to process stdout.

  -Unattended: pass Unreal's -unattended (automation mode: fewer prompts; some builds limit normal editor interaction).
    Default is OFF so you can click the running editor and watch tools run. Use -Unattended for CI or when you need fully non-interactive shutdown.

  Harness HTTP timeout + per-segment sync wait are hardcoded in UnrealAiWaitTimePolicy.h (namespace UnrealAiWaitTime; not .env / not script args).
  Plan-mode applies that budget per planner pass and per DAG node.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ScenarioFolder,
    [string]$EngineRoot = '',
    [string]$ProjectRoot = '',
    [string]$SuiteFileName = 'suite.json',
    [int]$MaxLlmRounds = 0,
    [int]$MaxTurnTokens = 0,
    [int]$EditorExitTimeoutSec = 0,
    [int]$MaxExecCmdChars = 7000,
    [int]$MaxSuites = 0,
    [switch]$ForceSingleSession,
    [switch]$CloseAllUnrealEditorsOnExit,
    [switch]$DryRun,
    [switch]$SkipClassification,
    [switch]$ReduceContextNoise,
    [switch]$MirrorEditorProcessStreamsToHost,
    [switch]$LiveProjectLog,
    [switch]$NoLiveProjectLog,
    [switch]$Unattended,
    [switch]$BringEditorToForeground
)

$ErrorActionPreference = 'Stop'
$batchRunsRoot = $null
$batchRunIndexUsed = 0
# Default: Unreal -log + Saved/Logs only; use -LiveProjectLog to stream project log into this PowerShell (old Cursor-centric default).
$Script:StreamProjectLogToHost = $false
if ($LiveProjectLog) { $Script:StreamProjectLogToHost = $true }
if ($NoLiveProjectLog) { $Script:StreamProjectLogToHost = $false }
$Script:SpawnedEditorPids = @()
$Script:RunStartUtc = (Get-Date).ToUniversalTime()
$Script:BatchEndBrief = $null

$RepoRootForEnv = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $RepoRootForEnv 'scripts\Import-RepoDotenv.ps1')
Import-RepoDotenv -RepoRoot $RepoRootForEnv
. (Join-Path $RepoRootForEnv 'scripts\Set-UnrealAiHeadedToolPackDefaults.ps1')

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = $RepoRootForEnv
}
if (-not (Test-Path (Join-Path $ProjectRoot 'blank.uproject'))) {
    Write-Error "Could not locate blank.uproject under ProjectRoot=$ProjectRoot"
}
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = if ($env:UE_ENGINE_ROOT) { $env:UE_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }
}

$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$UProject = Join-Path $ProjectRoot 'blank.uproject'
if (-not (Test-Path $EditorExe)) {
    Write-Error "UnrealEditor.exe not found: $EditorExe (set UE_ENGINE_ROOT or pass -EngineRoot)."
}

function Resolve-ScenarioFolder {
    param([string]$PathLike)
    if ([string]::IsNullOrWhiteSpace($PathLike)) {
        Write-Error 'ScenarioFolder is required.'
    }
    if ([System.IO.Path]::IsPathRooted($PathLike)) {
        return (Resolve-Path -LiteralPath $PathLike).Path
    }
    $base = Join-Path $ProjectRoot 'tests\long-running-tests'
    $candidate = Join-Path $base $PathLike
    if (Test-Path -LiteralPath $candidate) {
        return (Resolve-Path -LiteralPath $candidate).Path
    }
    if (Test-Path -LiteralPath $PathLike) {
        return (Resolve-Path -LiteralPath $PathLike).Path
    }
    Write-Error "ScenarioFolder not found: $PathLike"
}

function Get-SuitePathSlug {
    param(
        [string]$ScenarioRootAbs,
        [string]$SuiteFileFullPath
    )
    $root = $ScenarioRootAbs.TrimEnd('\')
    $full = $SuiteFileFullPath
    if ($full.Length -le $root.Length) {
        return 'suite'
    }
    $rel = $full.Substring($root.Length).TrimStart('\')
    $slug = $rel -replace '\\', '__' -replace '/', '__'
    $slug = [System.IO.Path]::GetFileNameWithoutExtension($slug)
    if ([string]::IsNullOrWhiteSpace($slug)) {
        $slug = 'suite'
    }
    foreach ($ch in [System.IO.Path]::GetInvalidFileNameChars()) {
        $slug = $slug.Replace([string]$ch, '_')
    }
    return $slug
}

function ConvertTo-ShortWinPath {
    param([string]$LiteralPath)
    if ([string]::IsNullOrWhiteSpace($LiteralPath)) { return $LiteralPath }
    try {
        if (-not (Test-Path -LiteralPath $LiteralPath)) { return $LiteralPath }
        $full = (Resolve-Path -LiteralPath $LiteralPath).Path
        $fso = New-Object -ComObject Scripting.FileSystemObject
        if ((Get-Item -LiteralPath $full).PSIsContainer) {
            return $fso.GetFolder($full).ShortPath
        }
        return $fso.GetFile($full).ShortPath
    }
    catch {
        return $LiteralPath
    }
}

function Map-TypeToMode {
    param([string]$TypeValue, [string]$FallbackType)
    $t = if ([string]::IsNullOrWhiteSpace($TypeValue)) { $FallbackType } else { $TypeValue }
    $v = $t.Trim().ToLowerInvariant()
    switch ($v) {
        'ask' { return 'ask' }
        'agent' { return 'agent' }
        'plan' { return 'plan' }
        default { throw "Unsupported type '$t'. Allowed: ask, agent, plan." }
    }
}

function Copy-ContextDecisionLogsForThread {
    param(
        [string]$ProjectRootPath,
        [string]$ThreadId,
        [string]$DestRunDir
    )
    if ([string]::IsNullOrWhiteSpace($ThreadId)) { return 0 }
    $src = Join-Path $ProjectRootPath ("Saved\UnrealAiEditor\ContextDecisionLogs\{0}" -f $ThreadId)
    if (-not (Test-Path -LiteralPath $src)) { return 0 }
    $dest = Join-Path $DestRunDir 'context_decision_logs'
    if (Test-Path -LiteralPath $dest) {
        Remove-Item -LiteralPath $dest -Recurse -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $count = 0
    $files = Get-ChildItem -LiteralPath $src -File -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -like '*.jsonl' -or $_.Name -like '*-summary.md'
    }
    foreach ($f in $files) {
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $dest $f.Name) -Force
        $count++
    }
    return $count
}

function Test-RunJsonlFinished {
    param([string]$RunJsonlPath)
    if ([string]::IsNullOrWhiteSpace($RunJsonlPath)) { return $false }
    if (-not (Test-Path -LiteralPath $RunJsonlPath)) { return $false }
    try {
        $raw = Get-Content -LiteralPath $RunJsonlPath -Raw -Encoding UTF8 -ErrorAction Stop
        return $raw -match '"type"\s*:\s*"run_finished"'
    }
    catch {
        return $false
    }
}

function Get-RunJsonlQuickStats {
    param([string]$RunJsonlPath)
    $stats = [ordered]@{
        exists = $false
        has_run_started = $false
        has_run_finished = $false
        tool_finish_count = 0
        tool_fail_count = 0
        blocker_hint_count = 0
        line_count = 0
    }
    if ([string]::IsNullOrWhiteSpace($RunJsonlPath)) {
        return $stats
    }
    if (-not (Test-Path -LiteralPath $RunJsonlPath)) {
        return $stats
    }
    $stats.exists = $true
    try {
        $raw = Get-Content -LiteralPath $RunJsonlPath -Raw -Encoding UTF8 -ErrorAction Stop
        if ([string]::IsNullOrWhiteSpace($raw)) {
            return $stats
        }
        $stats.has_run_started = ($raw -match '"type"\s*:\s*"run_started"')
        $stats.has_run_finished = ($raw -match '"type"\s*:\s*"run_finished"')
        $stats.tool_finish_count = [regex]::Matches($raw, '"type"\s*:\s*"tool_finish"').Count
        $stats.tool_fail_count = [regex]::Matches($raw, '"type":"tool_finish"[^\r\n]*"success":false').Count
        $stats.blocker_hint_count = [regex]::Matches($raw, 'Blocked Summary|explicit_blocker').Count
        $stats.line_count = [regex]::Matches($raw, '\r?\n').Count + 1
    }
    catch {}
    return $stats
}

function Stop-TrackedEditors {
    foreach ($editorPid in @($Script:SpawnedEditorPids)) {
        try {
            $proc = Get-Process -Id $editorPid -ErrorAction SilentlyContinue
            if ($proc) {
                try { [void]$proc.CloseMainWindow() } catch {}
                try { $proc.Refresh() } catch {}
                if (-not $proc.WaitForExit(1500)) {
                    Stop-UnrealEditorProcessTree -Proc $proc
                }
            }
        } catch {}
    }
}

function Stop-AllUnrealEditorsHard {
    $all = Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue
    foreach ($p in @($all)) {
        try {
            try { [void]$p.CloseMainWindow() } catch {}
            if (-not $p.WaitForExit(1500)) {
                try { $p.Kill() } catch {}
            }
        } catch {}
    }
}

function Stop-UnrealEditorProcessTree {
    param(
        [System.Diagnostics.Process]$Proc
    )
    if (-not $Proc) { return }
    try { $Proc.Refresh() } catch {}
    $id = $Proc.Id
    if ($id -le 0) { return }
    try {
        # Prefer tree kill on Windows so child tasks do not keep the session alive.
        $tk = Start-Process -FilePath 'taskkill.exe' -ArgumentList @('/PID', [string]$id, '/T', '/F') -PassThru -NoNewWindow -Wait -ErrorAction SilentlyContinue
    } catch {}
    try { $Proc.Refresh() } catch {}
    if (-not $Proc.HasExited) {
        try { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue } catch {}
    }
    try { $null = $Proc.WaitForExit(8000) } catch {}
}

function Get-UnrealProjectPrimaryLogPath {
    param([string]$ProjectRootPath)
    $logsDir = Join-Path $ProjectRootPath 'Saved\Logs'
    if (-not (Test-Path -LiteralPath $logsDir)) {
        return $null
    }
    $uproject = Get-ChildItem -LiteralPath $ProjectRootPath -Filter '*.uproject' -File -ErrorAction SilentlyContinue | Select-Object -First 1
    $base = if ($uproject) { $uproject.BaseName } else { 'blank' }
    $primary = Join-Path $logsDir ("{0}.log" -f $base)
    if (Test-Path -LiteralPath $primary) {
        return $primary
    }
    $newest = Get-ChildItem -LiteralPath $logsDir -Filter '*.log' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($newest) {
        return $newest.FullName
    }
    return $null
}

function Save-UnrealEditorSessionLogs {
    <#
      After an editor exits, copy the main Unreal project log (Saved/Logs/<project>.log) into the run output folder.
      This is where headed runs put the same content as the on-screen log console (Output Log / log window).
    #>
    param(
        [string]$ProjectRootPath,
        [string]$DestinationDir,
        [string]$DestFileName = 'editor_console_saved.log'
    )
    if ([string]::IsNullOrWhiteSpace($DestinationDir)) {
        return
    }
    if (-not (Test-Path -LiteralPath $DestinationDir)) {
        New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
    }
    $src = Get-UnrealProjectPrimaryLogPath -ProjectRootPath $ProjectRootPath
    if (-not $src) {
        Write-Warning ("Save-UnrealEditorSessionLogs: no Saved\Logs\*.log under {0}" -f $ProjectRootPath)
        return
    }
    $dest = Join-Path $DestinationDir $DestFileName
    try {
        Copy-Item -LiteralPath $src -Destination $dest -Force
    }
    catch {
        Write-Warning ("Save-UnrealEditorSessionLogs: failed to copy {0} -> {1}: {2}" -f $src, $dest, $_)
    }
}

function Stop-UnrealEditorConsoleTee {
    param(
        [System.Diagnostics.Process]$Process,
        [System.IO.StreamWriter]$OutWriter,
        [System.IO.StreamWriter]$ErrWriter,
        [string]$SourceIdOut,
        [string]$SourceIdErr
    )
    if ($null -ne $Process) {
        try {
            if (-not $Process.HasExited) { $null = $Process.WaitForExit(5000) }
        }
        catch { }
        try { $Process.CancelOutputRead() } catch { }
        try { $Process.CancelErrorRead() } catch { }
    }
    Start-Sleep -Milliseconds 250
    Unregister-Event -SourceIdentifier $SourceIdOut -ErrorAction SilentlyContinue
    Unregister-Event -SourceIdentifier $SourceIdErr -ErrorAction SilentlyContinue
    try {
        if ($null -ne $OutWriter) {
            $OutWriter.Flush()
            $OutWriter.Dispose()
        }
    }
    catch { }
    try {
        if ($null -ne $ErrWriter) {
            $ErrWriter.Flush()
            $ErrWriter.Dispose()
        }
    }
    catch { }
}

function Try-BringUnrealMainWindowToForeground {
    param([System.Diagnostics.Process]$Proc)
    if ($null -eq $Proc) { return }
    try {
        $h = [IntPtr]::Zero
        for ($i = 0; $i -lt 20; $i++) {
            try { $Proc.Refresh() } catch { }
            if ($Proc.HasExited) { return }
            $h = $Proc.MainWindowHandle
            if ($h -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 350
        }
        if ($h -eq [IntPtr]::Zero) { return }
        if (-not ('UnrealAiHarness.NativeWin32' -as [type])) {
            Add-Type -Namespace UnrealAiHarness -Name NativeWin32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr hWnd, int nCmdShow);
'@ -ErrorAction Stop
        }
        $SW_RESTORE = 9
        [void][UnrealAiHarness.NativeWin32]::ShowWindow($h, $SW_RESTORE)
        [void][UnrealAiHarness.NativeWin32]::SetForegroundWindow($h)
    }
    catch { }
}

function Invoke-HeadedEditorDynamic {
    param(
        [string]$ExecCmds,
        [string[]]$ExpectedRunJsonls,
        [int]$TurnCount,
        [string]$SessionLogDir = '',
        [string]$SessionLogFileSuffix = '',
        [bool]$MirrorProcessStreamsToHost = $false,
        [bool]$LiveProjectLogToHost = $false,
        [bool]$BringEditorToForeground = $false
    )
    # Harness uses UE_LOG(LogTemp, ...). Process stdout/stderr are redirected for tee files; Unreal does not always mirror
    # UE_LOG to those pipes. Prefer Unreal -log + Saved/Logs; optional LiveProjectLogToHost tails the file into PowerShell.
    $editorCliArgs = [System.Collections.Generic.List[string]]::new()
    [void]$editorCliArgs.Add("`"$UProject`"")
    [void]$editorCliArgs.Add('-nop4')
    [void]$editorCliArgs.Add('-nosplash')
    [void]$editorCliArgs.Add('-log')
    if ($Unattended) {
        [void]$editorCliArgs.Add('-unattended')
    }
    [void]$editorCliArgs.Add('-LogCmds=LogTemp Verbose, LogAutomationController Verbose, LogAutomationCommandLine Verbose')
    [void]$editorCliArgs.Add("-ExecCmds=`"$ExecCmds`"")

    $useWatchdog = ($EditorExitTimeoutSec -gt 0)
    $watchdogDeadlineUtc = if ($useWatchdog) { (Get-Date).ToUniversalTime().AddSeconds($EditorExitTimeoutSec) } else { $null }
    Write-Host ("Running headed editor... mode={0}" -f $(if ($useWatchdog) { "dynamic+watchdog" } else { "dynamic-no-watchdog" })) -ForegroundColor Cyan
    if ($Unattended) {
        Write-Host 'Unreal -unattended: automation mode (viewport interaction may be limited in some builds).' -ForegroundColor DarkYellow
    }
    else {
        Write-Host 'Interactive editor: -unattended omitted — click the viewport to watch (pass -Unattended for CI / fully non-interactive).' -ForegroundColor DarkGray
    }
    $existingEditorPids = @((Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id))

    $stdOutPath = $null
    $stdErrPath = $null
    if (-not [string]::IsNullOrWhiteSpace($SessionLogDir)) {
        New-Item -ItemType Directory -Force -Path $SessionLogDir | Out-Null
        $suffix = if ([string]::IsNullOrWhiteSpace($SessionLogFileSuffix)) { '' } else { $SessionLogFileSuffix }
        $stdOutPath = Join-Path $SessionLogDir ("editor_console_stdout{0}.txt" -f $suffix)
        $stdErrPath = Join-Path $SessionLogDir ("editor_console_stderr{0}.txt" -f $suffix)
    }

    $teeOutWriter = $null
    $teeErrWriter = $null
    $teeSourceOut = $null
    $teeSourceErr = $null
    $launcher = $null
    $harnessProgressStartedAt = $null

    if ($null -ne $stdOutPath -and $null -ne $stdErrPath) {
        if ($MirrorProcessStreamsToHost) {
            Write-Host 'Editor process stdout/stderr: mirroring to this console + editor_console_stdout/stderr .txt (UE_LOG still primarily in Unreal -log window + Saved/Logs).' -ForegroundColor DarkGray
        }
        else {
            Write-Host 'Harness progress: watch Unreal -log window and Saved/Logs; pass -LiveProjectLog to tail project log here; stdout/stderr tee -> editor_console_stdout/stderr .txt only.' -ForegroundColor DarkGray
        }
        $harnessProgressStartedAt = Get-Date
        $teeOutWriter = New-Object System.IO.StreamWriter($stdOutPath, $false, [System.Text.UTF8Encoding]::new($false))
        $teeErrWriter = New-Object System.IO.StreamWriter($stdErrPath, $false, [System.Text.UTF8Encoding]::new($false))
        $teeOutWriter.AutoFlush = $true
        $teeErrWriter.AutoFlush = $true
        $teeGuid = [guid]::NewGuid().ToString('N').Substring(0, 8)
        $teeSourceOut = "UnrealAiHarnessTeeOut_$teeGuid"
        $teeSourceErr = "UnrealAiHarnessTeeErr_$teeGuid"

        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $EditorExe
        $psi.Arguments = [string]::Join(' ', $editorCliArgs)
        $psi.WorkingDirectory = $ProjectRoot
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        # Do not redirect stdin: piping stdin can confuse Windows focus/input for GUI processes (user could not click the viewport reliably).
        $psi.RedirectStandardInput = $false
        $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
        $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8
        # Do not hide the process window: GUI editor should stay a normal top-level window; -unattended is what limits interaction.
        $psi.CreateNoWindow = $false

        $launcher = New-Object System.Diagnostics.Process
        $launcher.EnableRaisingEvents = $true
        $launcher.StartInfo = $psi

        $teeCtxOut = [ordered]@{ Writer = $teeOutWriter; MirrorToHost = $MirrorProcessStreamsToHost }
        $teeCtxErr = [ordered]@{ Writer = $teeErrWriter; MirrorToHost = $MirrorProcessStreamsToHost }
        $null = Register-ObjectEvent -InputObject $launcher -EventName OutputDataReceived -SourceIdentifier $teeSourceOut -MessageData $teeCtxOut -Action {
            $e = $Event.SourceEventArgs
            if ($null -eq $e.Data) { return }
            $ctx = $Event.MessageData
            $ctx.Writer.WriteLine($e.Data)
            if ($ctx.MirrorToHost) {
                Microsoft.PowerShell.Utility\Write-Host $e.Data
            }
        }
        $null = Register-ObjectEvent -InputObject $launcher -EventName ErrorDataReceived -SourceIdentifier $teeSourceErr -MessageData $teeCtxErr -Action {
            $e = $Event.SourceEventArgs
            if ($null -eq $e.Data) { return }
            $ctx = $Event.MessageData
            $ctx.Writer.WriteLine($e.Data)
            if ($ctx.MirrorToHost) {
                Microsoft.PowerShell.Utility\Write-Host $e.Data -ForegroundColor DarkYellow
            }
        }

        [void]$launcher.Start()
        $launcher.BeginOutputReadLine()
        $launcher.BeginErrorReadLine()
    }
    else {
        # Session log dir missing — no tee paths; launch without redirect (rare for this script).
        $spArgs = @{
            FilePath         = $EditorExe
            ArgumentList     = @($editorCliArgs)
            WorkingDirectory = $ProjectRoot
            PassThru         = $true
        }
        $launcher = Start-Process @spArgs
    }

    Start-Sleep -Milliseconds 600
    try { $launcher.Refresh() } catch {}

    $p = $launcher
    $teeCleanupDone = $false
    if ($launcher.HasExited -and $null -ne $teeSourceOut) {
        Stop-UnrealEditorConsoleTee -Process $launcher -OutWriter $teeOutWriter -ErrWriter $teeErrWriter -SourceIdOut $teeSourceOut -SourceIdErr $teeSourceErr
        $teeCleanupDone = $true
    }
    if ($launcher.HasExited) {
        # UE may hand off to another editor process and exit immediately.
        $currentEditors = @(Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue | Sort-Object StartTime -Descending)
        $newCandidates = @($currentEditors | Where-Object { $existingEditorPids -notcontains $_.Id })
        if ($newCandidates.Count -gt 0) {
            $p = $newCandidates[0]
            Write-Host ("Launcher exited quickly; attached to new UnrealEditor pid={0}" -f $p.Id) -ForegroundColor DarkYellow
            if ($teeCleanupDone) {
                Write-Host 'Note: stdout/stderr tee was tied to the launcher; live console output may stop until the next run (Unreal log is still copied at end).' -ForegroundColor DarkGray
            }
        } elseif ($currentEditors.Count -gt 0) {
            # Handoff may target an already-running process (single-instance behavior).
            $p = $currentEditors[0]
            Write-Host ("Launcher exited quickly; attached to existing UnrealEditor pid={0}" -f $p.Id) -ForegroundColor DarkYellow
            if ($teeCleanupDone) {
                Write-Host 'Note: stdout/stderr tee was tied to the launcher; live console output may stop until the next run (Unreal log is still copied at end).' -ForegroundColor DarkGray
            }
        } else {
            $exitCodeText = if ($null -ne $launcher.ExitCode) { [string]$launcher.ExitCode } else { "<unknown>" }
            Write-Warning ("UnrealEditor launcher exited immediately with code {0}; no editor process found." -f $exitCodeText)
        }
    }
    if ($Script:SpawnedEditorPids -notcontains $p.Id) {
        $Script:SpawnedEditorPids += $p.Id
    }

    if ($BringEditorToForeground -and -not $Unattended) {
        Write-Host 'Bringing Unreal Editor main window forward (best-effort; click the editor if focus stays on PowerShell).' -ForegroundColor DarkGray
        Try-BringUnrealMainWindowToForeground -Proc $p
        Start-Sleep -Milliseconds 2200
        Try-BringUnrealMainWindowToForeground -Proc $p
    }

    $tailJob = $null
    if ($LiveProjectLogToHost) {
        Write-Host 'Live log: streaming project Saved/Logs *.log -> this console (the separate Unreal -log window may be sparse when stdout is redirected; UE_LOG still appends to Saved/Logs).' -ForegroundColor DarkCyan
        $tailJob = Start-Job -ScriptBlock {
            param([string]$Root)
            function Get-PrimaryLogPath([string]$ProjectRootPath) {
                $logsDir = Join-Path $ProjectRootPath 'Saved\Logs'
                if (-not (Test-Path -LiteralPath $logsDir)) { return $null }
                $uproject = Get-ChildItem -LiteralPath $ProjectRootPath -Filter '*.uproject' -File -ErrorAction SilentlyContinue | Select-Object -First 1
                $base = if ($uproject) { $uproject.BaseName } else { 'blank' }
                $primary = Join-Path $logsDir ("{0}.log" -f $base)
                if (Test-Path -LiteralPath $primary) { return $primary }
                $newest = Get-ChildItem -LiteralPath $logsDir -Filter '*.log' -File -ErrorAction SilentlyContinue |
                    Sort-Object LastWriteTime -Descending | Select-Object -First 1
                if ($newest) { return $newest.FullName }
                return $null
            }
            $logPath = $null
            for ($i = 0; $i -lt 600; $i++) {
                $logPath = Get-PrimaryLogPath -ProjectRootPath $Root
                if ($null -ne $logPath) { break }
                Start-Sleep -Milliseconds 200
            }
            if ($null -eq $logPath) {
                Write-Output '[live-log] Timed out waiting for Saved\Logs\*.log'
                return
            }
            Get-Content -LiteralPath $logPath -Wait -Tail 40 -Encoding UTF8
        } -ArgumentList $ProjectRoot
    }

    try {
        if ($null -eq $harnessProgressStartedAt) {
            $harnessProgressStartedAt = Get-Date
        }
        $prevConsoleWindowTitle = $null
        try { $prevConsoleWindowTitle = $Host.UI.RawUI.WindowTitle } catch { }
        $lastHarnessTimerUiUtc = [DateTime]::MinValue
        $lastHarnessProgressLineUtc = $harnessProgressStartedAt
        $harnessTimerStatusLine = $false
        $allFinished = $false
        $totalExpectedTurns = $ExpectedRunJsonls.Count
        # Long suites / noisy modes: occasional full lines so ETA is visible even when -LiveProjectLog fills the console.
        $harnessProgressFullLineSec = if ($LiveProjectLogToHost -or $MirrorProcessStreamsToHost) { 25 } elseif ($totalExpectedTurns -ge 12) { 55 } else { 0 }
        while ($true) {
            if ($null -ne $tailJob) {
                $chunk = Receive-Job -Job $tailJob -ErrorAction SilentlyContinue
                if ($null -ne $chunk) {
                    foreach ($line in @($chunk)) {
                        if ($null -ne $line) {
                            Write-Host $line
                        }
                    }
                }
            }
            $tickNow = Get-Date
            $elapsed = $tickNow - $harnessProgressStartedAt
            $done = 0
            foreach ($j in $ExpectedRunJsonls) {
                if (Test-RunJsonlFinished -RunJsonlPath $j) { $done++ }
            }
            $etaSuffix = ''
            if ($totalExpectedTurns -gt 0 -and $done -gt 0 -and $done -lt $totalExpectedTurns) {
                $avgSec = $elapsed.TotalSeconds / [double]$done
                $remSec = $avgSec * ($totalExpectedTurns - $done)
                if ($remSec -lt 0) { $remSec = 0 }
                $rem = [TimeSpan]::FromSeconds($remSec)
                $etaH = [int][math]::Floor($rem.TotalHours)
                $etaSuffix = ' | ETA ~{0:00}:{1:00}:{2:00}' -f $etaH, $rem.Minutes, $rem.Seconds
            }
            elseif ($totalExpectedTurns -gt 0 -and $done -eq 0) {
                $etaSuffix = ' | ETA: after 1st turn'
            }
            if (($tickNow - $lastHarnessTimerUiUtc).TotalMilliseconds -ge 500) {
                $lastHarnessTimerUiUtc = $tickNow
                $h = [int][math]::Floor($elapsed.TotalHours)
                $elapsedStr = '{0:00}:{1:00}:{2:00}' -f $h, $elapsed.Minutes, $elapsed.Seconds
                $titleExtra = if ($totalExpectedTurns -gt 0) { "$done/$totalExpectedTurns | $elapsedStr$etaSuffix" } else { "elapsed $elapsedStr" }
                try {
                    $Host.UI.RawUI.WindowTitle = "Unreal AI harness | $titleExtra"
                }
                catch { }
                $showInlineTimer = (-not $LiveProjectLogToHost) -and (-not $MirrorProcessStreamsToHost)
                if ($showInlineTimer) {
                    if ($totalExpectedTurns -gt 0) {
                        Write-Host "`rHarness turns $done/$totalExpectedTurns | $elapsedStr$etaSuffix  " -NoNewline -ForegroundColor DarkGray
                    }
                    else {
                        Write-Host "`rHarness elapsed $elapsedStr  " -NoNewline -ForegroundColor DarkGray
                    }
                    $harnessTimerStatusLine = $true
                }
            }
            if ($harnessProgressFullLineSec -gt 0 -and ($tickNow - $lastHarnessProgressLineUtc).TotalSeconds -ge $harnessProgressFullLineSec) {
                $lastHarnessProgressLineUtc = $tickNow
                $h = [int][math]::Floor($elapsed.TotalHours)
                $elapsedStr = '{0:00}:{1:00}:{2:00}' -f $h, $elapsed.Minutes, $elapsed.Seconds
                Write-Host ''
                Write-Host ("[harness] turns {0}/{1} | wall {2}{3}" -f $done, $totalExpectedTurns, $elapsedStr, $etaSuffix) -ForegroundColor Cyan
            }
            try { $p.Refresh() } catch {}
            if ($p.HasExited) { break }
            $now = (Get-Date).ToUniversalTime()
            if ($totalExpectedTurns -gt 0 -and $done -ge $totalExpectedTurns) {
                $allFinished = $true
                break
            }
            if ($useWatchdog -and $now -ge $watchdogDeadlineUtc) {
                Write-Warning ("Watchdog timeout waiting for run completion ({0}/{1} turns finished). Forcing shutdown." -f $done, $totalExpectedTurns)
                break
            }
            Start-Sleep -Milliseconds 200
        }

        if ($harnessTimerStatusLine) {
            Write-Host ''
        }

        if (-not $p.HasExited) {
            try { [void]$p.CloseMainWindow() } catch {}
            try { $p.Refresh() } catch {}
            if (-not $p.WaitForExit(2500)) {
                Stop-UnrealEditorProcessTree -Proc $p
            }
        }
        try { $p.Refresh() } catch {}
        if (-not $p.HasExited) {
            Stop-UnrealEditorProcessTree -Proc $p
        }

        if (-not $teeCleanupDone -and $null -ne $teeSourceOut) {
            Stop-UnrealEditorConsoleTee -Process $launcher -OutWriter $teeOutWriter -ErrWriter $teeErrWriter -SourceIdOut $teeSourceOut -SourceIdErr $teeSourceErr
            $teeCleanupDone = $true
        }

        if (-not [string]::IsNullOrWhiteSpace($SessionLogDir)) {
            $logName = if ([string]::IsNullOrWhiteSpace($SessionLogFileSuffix)) {
                'editor_console_saved.log'
            } else {
                ('editor_console_saved{0}.log' -f $SessionLogFileSuffix)
            }
            Save-UnrealEditorSessionLogs -ProjectRootPath $ProjectRoot -DestinationDir $SessionLogDir -DestFileName $logName
        }

        $exitCode = if ($p.HasExited) { [int]$p.ExitCode } else { -1003 }
        return [ordered]@{
            exit_code = $exitCode
            all_finished = $allFinished
        }
    }
    finally {
        try {
            if ($null -ne $prevConsoleWindowTitle) {
                $Host.UI.RawUI.WindowTitle = $prevConsoleWindowTitle
            }
        }
        catch { }
        if ($null -ne $tailJob) {
            Stop-Job -Job $tailJob -ErrorAction SilentlyContinue
            $rest = Receive-Job -Job $tailJob -ErrorAction SilentlyContinue
            if ($null -ne $rest) {
                foreach ($line in @($rest)) {
                    if ($null -ne $line) {
                        Write-Host $line
                    }
                }
            }
            Remove-Job -Job $tailJob -Force -ErrorAction SilentlyContinue
        }
    }
}

$scenarioAbs = Resolve-ScenarioFolder $ScenarioFolder
$filter = [System.IO.Path]::GetFileName($SuiteFileName)
if ([string]::IsNullOrWhiteSpace($filter)) {
    $filter = 'suite.json'
}

$suiteFiles = @(
    Get-ChildItem -LiteralPath $scenarioAbs -Recurse -File -Filter $filter -ErrorAction Stop |
        Sort-Object FullName
)
if ($suiteFiles.Count -eq 0) {
    Write-Error "No '$filter' files found under: $scenarioAbs"
}
$suitesDiscoveredCount = $suiteFiles.Count
if ($MaxSuites -ne 0) {
    $want = [Math]::Abs($MaxSuites)
    $take = [Math]::Min($want, $suitesDiscoveredCount)
    if ($take -lt $suitesDiscoveredCount) {
        $suiteFiles = @($suiteFiles[0..($take - 1)])
    }
    Write-Host ("MaxSuites={0}: running {1} of {2} suite file(s) (sorted by path)." -f $MaxSuites, $suiteFiles.Count, $suitesDiscoveredCount) -ForegroundColor Yellow
}

# One folder per batch: tests/long-running-tests/runs/run-<index>-<local timestamp> (index monotonic across all scenarios).
$batchStartedLocal = Get-Date
$batchStampCore = $batchStartedLocal.ToString('yyyyMMdd-HHmmss_fff')
$runsParent = Join-Path $PSScriptRoot 'runs'
if (-not (Test-Path -LiteralPath $runsParent)) {
    New-Item -ItemType Directory -Force -Path $runsParent | Out-Null
}
$maxRunIndex = 0
Get-ChildItem -LiteralPath $runsParent -Directory -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.Name -match '^run-(\d+)-') {
        $n = [int]$Matches[1]
        if ($n -gt $maxRunIndex) {
            $maxRunIndex = $n
        }
    }
}
$batchRunIndex = $maxRunIndex + 1
while ($true) {
    $batchFolderBase = 'run-{0}-{1}' -f $batchRunIndex, $batchStampCore
    $batchRunsRoot = Join-Path $runsParent $batchFolderBase
    if (-not (Test-Path -LiteralPath $batchRunsRoot)) {
        break
    }
    $batchRunIndex++
}
$batchRunIndexUsed = $batchRunIndex
New-Item -ItemType Directory -Force -Path $batchRunsRoot | Out-Null
$batchStamp = [System.IO.Path]::GetFileName($batchRunsRoot)
$batchStartedLocalIso = $batchStartedLocal.ToString('o')
$batchStartedUtcIso = $batchStartedLocal.ToUniversalTime().ToString('o')
Write-Host ("Batch output: run_index={0} | {1}" -f $batchRunIndexUsed, $batchRunsRoot) -ForegroundColor Cyan
Write-Host 'Harness limits: UnrealAiWaitTime in UnrealAiWaitTimePolicy.h (HttpRequestTimeoutSec, HarnessSyncWaitMs, etc.).' -ForegroundColor DarkGray

$allStatuses = @()
$previousThreadId = $null

foreach ($jf in $suiteFiles) {
    $raw = Get-Content -LiteralPath $jf.FullName -Raw -Encoding UTF8
    $doc = $raw | ConvertFrom-Json
    $turns = @($doc.turns)
    if ($turns.Count -eq 0) {
        Write-Warning "Skipping $($jf.FullName): no turns[]"
        continue
    }

    $suiteExecParts = [System.Collections.Generic.List[string]]::new()
    $suiteExpectedRunJsonls = [System.Collections.Generic.List[string]]::new()
    if ($null -ne $previousThreadId) {
        $suiteExecParts.Add(('UnrealAi.ForgetThread "{0}"' -f $previousThreadId))
    }

    $suiteId = if ($doc.suite_id) { [string]$doc.suite_id } else { 'long_running_suite' }
    $defaultType = if ($doc.default_type) { [string]$doc.default_type } else { 'agent' }
    $threadId = [guid]::NewGuid().ToString()
    $pathSlug = Get-SuitePathSlug -ScenarioRootAbs $scenarioAbs -SuiteFileFullPath $jf.FullName

    $runRoot = Join-Path $batchRunsRoot $pathSlug
    $turnsRoot = Join-Path $runRoot 'turns'
    $msgRoot = Join-Path $runRoot 'turn_messages'
    New-Item -ItemType Directory -Force -Path $turnsRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $msgRoot | Out-Null
    Copy-Item -LiteralPath $jf.FullName -Destination (Join-Path $runRoot 'workflow-input.json') -Force
    [System.IO.File]::WriteAllText((Join-Path $runRoot 'thread_id.txt'), $threadId, (New-Object System.Text.UTF8Encoding($false)))

    $turnStatus = @()
    $i = 0
    foreach ($t in $turns) {
        $i++
        $req = [string]$t.request
        if ([string]::IsNullOrWhiteSpace($req)) {
            throw "File $($jf.Name): turn #$i missing request"
        }
        $mode = Map-TypeToMode -TypeValue ([string]$t.type) -FallbackType $defaultType
        $msgPath = Join-Path $msgRoot ("turn_{0:D2}.txt" -f $i)
        $stepDir = Join-Path $turnsRoot ("step_{0:D2}" -f $i)
        New-Item -ItemType Directory -Force -Path $stepDir | Out-Null
        [System.IO.File]::WriteAllText($msgPath, $req, (New-Object System.Text.UTF8Encoding($false)))
        $msgForCmd = ConvertTo-ShortWinPath $msgPath
        $stepForCmd = ConvertTo-ShortWinPath $stepDir
        $dumpArg = if ($ReduceContextNoise) { 'nodump' } else { 'dumpcontext' }
        $suiteExecParts.Add(('UnrealAi.RunAgentTurn "{0}" "{1}" "{2}" "{3}" {4}' -f $msgForCmd, $threadId, $mode, $stepForCmd, $dumpArg))
        $suiteExpectedRunJsonls.Add((Join-Path $stepDir 'run.jsonl'))
        $turnStatus += [ordered]@{
            turn_index = $i
            type = if ($t.type) { [string]$t.type } else { $defaultType }
            mode = $mode
            request = $req
            message_file = $msgPath
            step_dir = $stepDir
        }
    }

    $summary = [ordered]@{
        suite_id = $suiteId
        source_file = $jf.FullName
        path_slug = $pathSlug
        run_root = $runRoot
        batch_output_folder = $batchRunsRoot
        thread_id = $threadId
        started_utc = (Get-Date).ToUniversalTime().ToString('o')
        turn_count = $turnStatus.Count
        max_llm_rounds = $MaxLlmRounds
        max_turn_tokens = $MaxTurnTokens
        editor_exit_code = $null
        dry_run = [bool]$DryRun
        batch_stamp = $batchStamp
        batch_stamp_local_core = $batchStampCore
        batch_started_local = $batchStartedLocalIso
        batch_started_utc = $batchStartedUtcIso
        exec_parts = @($suiteExecParts)
        expected_run_jsonls = @($suiteExpectedRunJsonls)
        turns = $turnStatus
    }
    if ($doc.PSObject.Properties.Name -contains 'domain_scenario_refs') {
        $summary.domain_scenario_refs = @($doc.domain_scenario_refs)
    }
    if ($doc.PSObject.Properties.Name -contains 'coverage_notes') {
        $summary.coverage_notes = [string]$doc.coverage_notes
    }

    if ($DryRun) {
        $summary.editor_exit_code = 0
        $summary.all_turns_reached_terminal = $true
        $summary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')
        ($summary | ConvertTo-Json -Depth 8) + "`n" | Set-Content -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding UTF8
        $allStatuses += $summary
        Write-Host ("DryRun prepared: {0} | turns={1} | thread={2}" -f $pathSlug, $turnStatus.Count, $threadId) -ForegroundColor Yellow
        $previousThreadId = $threadId
        continue
    }

    $allStatuses += $summary
    $previousThreadId = $threadId
}

if (-not $DryRun -and $allStatuses.Count -eq 0) {
    Write-Error "No runnable suite turns (all files skipped?)."
}

$exitCode = 0
$dynamicDone = $true
$batchExitCode = 0
$batchAllJsonlsFinished = $false
$failedSuiteCount = 0
$Script:BatchSessionMode = 'single'
$Script:ExecCmdsLengthChars = 0
$Script:PerSuiteExitBySlug = @{}
try {
    if (-not $DryRun) {
        # UE can forward launch args to an already-running editor instance and exit immediately.
        # Ensure a clean single-editor session for deterministic headed harness execution.
        $existingEditors = @(Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue)
        if ($existingEditors.Count -gt 0) {
            Write-Host ("Closing {0} existing UnrealEditor process(es) before run..." -f $existingEditors.Count) -ForegroundColor DarkYellow
            Stop-AllUnrealEditorsHard
            Start-Sleep -Milliseconds 800
        }

        if ($MaxLlmRounds -gt 0 -or $MaxTurnTokens -ne 0) {
            Write-Warning "MaxLlmRounds/MaxTurnTokens are no longer applied via environment; configure maxAgentLlmRounds / maxAgentTurnTokens in the model profile (values recorded in summary.json only)."
        }
        $totalTurns = 0
        foreach ($suiteSummary in $allStatuses) {
            $totalTurns += @($suiteSummary.expected_run_jsonls).Count
        }
        Write-Host ("Batch: suites={0} total_turns={1} stamp={2}" -f $allStatuses.Count, $totalTurns, $batchStamp) -ForegroundColor Green
        foreach ($suiteSummary in $allStatuses) {
            Write-Host ("  Suite: {0} | turns={1} | thread={2}" -f $suiteSummary.path_slug, $suiteSummary.turn_count, $suiteSummary.thread_id) -ForegroundColor Green
        }
        if ($totalTurns -ge 8) {
            $hintLo = [math]::Max(3, [int][math]::Floor($totalTurns * 0.35))
            $hintHi = [math]::Max($hintLo + 1, [int][math]::Ceiling($totalTurns * 0.55))
            Write-Host ("  Wall time varies (LLM + tools); ~{0}-{1} min is a common band for {2} turns on a responsive API." -f $hintLo, $hintHi, $totalTurns) -ForegroundColor DarkGray
            Write-Host '  While the editor runs: PowerShell shows turns M/N + ETA (after turn 1); window title updates; [harness] lines every ~25-55s on long runs / live log.' -ForegroundColor DarkGray
        }

        $allExecParts = [System.Collections.Generic.List[string]]::new()
        $allExpectedRunJsonls = [System.Collections.Generic.List[string]]::new()
        foreach ($suiteSummary in $allStatuses) {
            foreach ($part in @($suiteSummary.exec_parts)) {
                $allExecParts.Add([string]$part)
            }
            foreach ($rj in @($suiteSummary.expected_run_jsonls)) {
                $allExpectedRunJsonls.Add([string]$rj)
            }
        }
        $allExecParts.Add('Quit')
        $execCmds = ($allExecParts -join ',')
        $execCmdLen = $execCmds.Length
        $batchSessionMode = 'single'
        if ($ForceSingleSession -and $execCmdLen -gt $MaxExecCmdChars) {
            Write-Error ("ExecCmds length is {0} chars (limit {1}). Shorten suites, raise -MaxExecCmdChars, or omit -ForceSingleSession to allow per-suite fallback." -f $execCmdLen, $MaxExecCmdChars)
        }
        if ($execCmdLen -gt $MaxExecCmdChars) {
            Write-Warning ("ExecCmds length {0} exceeds -MaxExecCmdChars ({1}); Windows command-line limits require one editor launch per suite (still sequential, ForgetThread preserved)." -f $execCmdLen, $MaxExecCmdChars)
            $batchSessionMode = 'per_suite'
            $batchExitCode = 0
            $batchAllJsonlsFinished = $true
            $perSuiteExits = @{}
            foreach ($suiteSummary in $allStatuses) {
                $suiteExpected = @($suiteSummary.expected_run_jsonls)
                $suiteParts = @($suiteSummary.exec_parts)
                $chunks = New-Object System.Collections.Generic.List[object]
                $current = New-Object System.Collections.Generic.List[string]
                foreach ($part in $suiteParts) {
                    $candidate = New-Object System.Collections.Generic.List[string]
                    foreach ($p in $current) { $candidate.Add($p) }
                    $candidate.Add([string]$part)
                    $candidateWithQuit = New-Object System.Collections.Generic.List[string]
                    foreach ($p in $candidate) { $candidateWithQuit.Add($p) }
                    $candidateWithQuit.Add('Quit')
                    $candidateLen = (($candidateWithQuit -join ',')).Length
                    if ($candidateLen -gt $MaxExecCmdChars -and $current.Count -gt 0) {
                        $chunks.Add(@($current))
                        $current = New-Object System.Collections.Generic.List[string]
                        $current.Add([string]$part)
                    }
                    else {
                        if ($candidateLen -gt $MaxExecCmdChars -and $current.Count -eq 0) {
                            Write-Error ("Suite {0}: single turn command exceeds -MaxExecCmdChars ({1}). Raise limit or shorten paths." -f $suiteSummary.path_slug, $MaxExecCmdChars)
                        }
                        $current = $candidate
                    }
                }
                if ($current.Count -gt 0) {
                    $chunks.Add(@($current))
                }
                if ($chunks.Count -eq 0) {
                    $chunks.Add(@())
                }

                $ex = 0
                $chunkIndex = 0
                foreach ($chunkParts in $chunks) {
                    $chunkIndex++
                    $suiteExec = [System.Collections.Generic.List[string]]::new()
                    foreach ($part in @($chunkParts)) { $suiteExec.Add([string]$part) }
                    $suiteExec.Add('Quit')
                    $suiteCmds = ($suiteExec -join ',')
                    Write-Host ("Running headed editor (per-suite) {0} chunk {1}/{2}" -f $suiteSummary.path_slug, $chunkIndex, $chunks.Count) -ForegroundColor Cyan
                    $chunkSuffix = if ($chunks.Count -gt 1) { ('_chunk{0:D2}' -f $chunkIndex) } else { '' }
                    $runResult = Invoke-HeadedEditorDynamic -ExecCmds $suiteCmds -ExpectedRunJsonls $suiteExpected -TurnCount $suiteExpected.Count -SessionLogDir $suiteSummary.run_root -SessionLogFileSuffix $chunkSuffix -MirrorProcessStreamsToHost:$MirrorEditorProcessStreamsToHost -LiveProjectLogToHost:$Script:StreamProjectLogToHost -BringEditorToForeground:$BringEditorToForeground
                    $chunkExit = [int]$runResult.exit_code
                    if ($ex -eq 0 -and $chunkExit -ne 0) {
                        $ex = $chunkExit
                    }
                    if (-not [bool]$runResult.all_finished) {
                        $batchAllJsonlsFinished = $false
                    }
                }
                $perSuiteExits[[string]$suiteSummary.path_slug] = $ex
                if ($batchExitCode -eq 0 -and $ex -ne 0) {
                    $batchExitCode = $ex
                }
            }
            $Script:PerSuiteExitBySlug = $perSuiteExits
        }
        else {
            Write-Host ("ExecCmds length={0} (limit {1}) - single editor session" -f $execCmdLen, $MaxExecCmdChars) -ForegroundColor DarkGray
            $runResult = Invoke-HeadedEditorDynamic -ExecCmds $execCmds -ExpectedRunJsonls @($allExpectedRunJsonls) -TurnCount $allExpectedRunJsonls.Count -SessionLogDir $batchRunsRoot -MirrorProcessStreamsToHost:$MirrorEditorProcessStreamsToHost -LiveProjectLogToHost:$Script:StreamProjectLogToHost -BringEditorToForeground:$BringEditorToForeground
            $batchExitCode = [int]$runResult.exit_code
            $batchAllJsonlsFinished = [bool]$runResult.all_finished
            $Script:PerSuiteExitBySlug = @{}
            $batchLogMaster = Join-Path $batchRunsRoot 'editor_console_saved.log'
            if (Test-Path -LiteralPath $batchLogMaster) {
                $dupN = 0
                foreach ($suiteSummary in $allStatuses) {
                    $perSuiteCopy = Join-Path $suiteSummary.run_root 'editor_console_full_batch.log'
                    try {
                        Copy-Item -LiteralPath $batchLogMaster -Destination $perSuiteCopy -Force
                        $dupN++
                    }
                    catch {
                        Write-Warning ("Could not copy batch editor log to {0}: {1}" -f $perSuiteCopy, $_)
                    }
                }
                if ($dupN -gt 0) {
                    Write-Host ("  Full-session Unreal log also copied per suite as editor_console_full_batch.log ({0} copies)." -f $dupN) -ForegroundColor DarkGray
                }
            }
        }
        $Script:BatchSessionMode = $batchSessionMode
        $Script:ExecCmdsLengthChars = $execCmdLen

        $failedSuiteCount = 0
        foreach ($suiteSummary in $allStatuses) {
            $suiteExpected = @($suiteSummary.expected_run_jsonls)
            $suiteTurnCount = $suiteExpected.Count
            $suiteDone = $true
            $suiteProducedCount = 0
            $suiteFinishedCount = 0
            $suiteStartedOnlyCount = 0
            $suiteToolFinishEvents = 0
            $suiteToolFailEvents = 0
            $suiteBlockerHints = 0
            if ($suiteTurnCount -gt 0) {
                foreach ($j in $suiteExpected) {
                    $quick = Get-RunJsonlQuickStats -RunJsonlPath $j
                    if ($quick.exists) { $suiteProducedCount++ }
                    if ($quick.has_run_finished) { $suiteFinishedCount++ }
                    if ($quick.has_run_started -and -not $quick.has_run_finished -and $quick.line_count -le 2) {
                        $suiteStartedOnlyCount++
                    }
                    $suiteToolFinishEvents += [int]$quick.tool_finish_count
                    $suiteToolFailEvents += [int]$quick.tool_fail_count
                    $suiteBlockerHints += [int]$quick.blocker_hint_count
                    if (-not $quick.has_run_finished) {
                        $suiteDone = $false
                    }
                }
            }

            $tid = $suiteSummary.thread_id
            foreach ($ts in $suiteSummary.turns) {
                $ts.run_jsonl_exists = Test-Path -LiteralPath (Join-Path $ts.step_dir 'run.jsonl')
                $ts.context_files = @(Get-ChildItem -LiteralPath $ts.step_dir -File -Filter 'context_window_*.txt' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
                $ts.context_decision_logs_copied = Copy-ContextDecisionLogsForThread -ProjectRootPath $ProjectRoot -ThreadId $tid -DestRunDir $ts.step_dir
            }

            $suiteProducedAny = $false
            foreach ($ts in $suiteSummary.turns) {
                if ($ts.run_jsonl_exists) { $suiteProducedAny = $true; break }
            }
            if (-not $suiteProducedAny -and $suiteTurnCount -gt 0) {
                Write-Warning ("Suite {0}: no run.jsonl files were produced. Likely ExecCmds rejection or editor exited early." -f $suiteSummary.path_slug)
                $suiteDone = $false
            }

            if ($batchSessionMode -eq 'per_suite' -and $Script:PerSuiteExitBySlug.ContainsKey([string]$suiteSummary.path_slug)) {
                $suiteSummary.editor_exit_code = $Script:PerSuiteExitBySlug[[string]$suiteSummary.path_slug]
            }
            else {
                $suiteSummary.editor_exit_code = $batchExitCode
            }
            $suiteSummary.all_turns_reached_terminal = $suiteDone
            $suiteSummary.run_jsonl_produced_count = $suiteProducedCount
            $suiteSummary.run_jsonl_finished_count = $suiteFinishedCount
            $suiteSummary.run_jsonl_started_only_count = $suiteStartedOnlyCount
            $suiteSummary.tool_finish_event_count = $suiteToolFinishEvents
            $suiteSummary.tool_fail_event_count = $suiteToolFailEvents
            $suiteSummary.blocker_hint_count = $suiteBlockerHints
            $suiteSummary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')
            ($suiteSummary | ConvertTo-Json -Depth 8) + "`n" | Set-Content -LiteralPath (Join-Path $suiteSummary.run_root 'summary.json') -Encoding UTF8

            if (-not $suiteDone) {
                $failedSuiteCount++
            }
        }

        $batchFailed = ($batchExitCode -ne 0) -or (-not $batchAllJsonlsFinished) -or ($failedSuiteCount -gt 0)
        if (-not $batchAllJsonlsFinished) {
            Write-Warning "Batch incomplete: not all expected run.jsonl files reached run_finished (sync timeout, crash, or stuck turn)."
        }
        if ($batchExitCode -ne 0) {
            Write-Warning ("Batch editor exit code: {0}" -f $batchExitCode)
        }

        # End-of-batch high-level diagnostics for quick triage.
        $totalExpected = 0
        $totalProduced = 0
        $totalFinished = 0
        $totalStartedOnly = 0
        $totalToolFinishes = 0
        $totalToolFails = 0
        $totalBlockers = 0
        foreach ($suiteSummary in $allStatuses) {
            $totalExpected += [int](@($suiteSummary.expected_run_jsonls).Count)
            $totalProduced += [int]$suiteSummary.run_jsonl_produced_count
            $totalFinished += [int]$suiteSummary.run_jsonl_finished_count
            $totalStartedOnly += [int]$suiteSummary.run_jsonl_started_only_count
            $totalToolFinishes += [int]$suiteSummary.tool_finish_event_count
            $totalToolFails += [int]$suiteSummary.tool_fail_event_count
            $totalBlockers += [int]$suiteSummary.blocker_hint_count
        }
        $elapsedSec = [Math]::Round(((Get-Date).ToUniversalTime() - $Script:RunStartUtc).TotalSeconds, 1)
        Write-Host ("Batch stats: expected_turns={0} produced_run_jsonl={1} finished_turns={2} missing_finished={3}" -f $totalExpected, $totalProduced, $totalFinished, ($totalExpected - $totalFinished)) -ForegroundColor DarkCyan
        Write-Host ("Batch signals: tool_finish_events={0} tool_fail_events={1} blocker_hints={2} started_only_turns={3} elapsed_sec={4}  (tool_finish_events = tool_finish lines in run.jsonl, not stream chunks)" -f $totalToolFinishes, $totalToolFails, $totalBlockers, $totalStartedOnly, $elapsedSec) -ForegroundColor DarkCyan

        $exitCode = if ($batchFailed) { 1 } else { 0 }
        $dynamicDone = -not $batchFailed
        $Script:BatchEndBrief = [ordered]@{
            suites = $allStatuses.Count
            expected_turns = $totalExpected
            finished_turns = $totalFinished
            tool_finish_events = $totalToolFinishes
            tool_calls = $totalToolFinishes
            tool_failures = $totalToolFails
            blocker_hints = $totalBlockers
            failed_suites = $failedSuiteCount
            batch_editor_exit = $batchExitCode
            all_jsonls_finished = $batchAllJsonlsFinished
            harness_exit = $exitCode
            all_terminal = $dynamicDone
            elapsed_sec = $elapsedSec
        }
    }
}
finally {
    Stop-TrackedEditors
    if ($CloseAllUnrealEditorsOnExit) {
        Stop-AllUnrealEditorsHard
    }
}

$suiteSummary = [ordered]@{
    scenario_folder = $scenarioAbs
    suite_file_name = $filter
    suite_file_count = $suiteFiles.Count
    suites_discovered = $suitesDiscoveredCount
    max_suites = $MaxSuites
    batch_stamp = $batchStamp
    batch_run_index = $batchRunIndexUsed
    runs_root = $runsParent
    batch_output_folder = $batchRunsRoot
    batch_stamp_local_core = $batchStampCore
    batch_started_local = $batchStartedLocalIso
    batch_started_utc = $batchStartedUtcIso
    batch_session_mode = $Script:BatchSessionMode
    exec_cmds_length_chars = $Script:ExecCmdsLengthChars
    max_exec_cmd_chars = $MaxExecCmdChars
    recursive_suite_discovery = $true
    started_utc = $(if ($Script:RunStartUtc) { $Script:RunStartUtc.ToString('o') } else { (Get-Date).ToUniversalTime().ToString('o') })
    files_processed = $allStatuses.Count
    runs = $allStatuses
}
if (-not $DryRun -and $allStatuses.Count -gt 0) {
    $suiteSummary['editor_exit_code'] = $exitCode
    $suiteSummary['all_turns_reached_terminal'] = $dynamicDone
    $suiteSummary['batch_editor_exit_code'] = $batchExitCode
    $suiteSummary['batch_all_expected_run_jsonls_finished'] = $batchAllJsonlsFinished
    $suiteSummary['failed_suite_count'] = $failedSuiteCount
    if ($Script:PerSuiteExitBySlug.Count -gt 0) {
        $suiteSummary['per_suite_editor_exit_codes'] = $Script:PerSuiteExitBySlug
    }
}
$summaryJson = ($suiteSummary | ConvertTo-Json -Depth 8) + "`n"
if ($null -ne $batchRunsRoot) {
    $summaryJson | Set-Content -LiteralPath (Join-Path $batchRunsRoot 'last-suite-summary.json') -Encoding UTF8
}

if ($null -ne $batchRunsRoot -and -not $DryRun -and $allStatuses.Count -gt 0 -and -not $SkipClassification) {
    $classifier = Join-Path $RepoRootForEnv 'tests\classify_harness_run_jsonl.py'
    if (Test-Path -LiteralPath $classifier) {
        Write-Host 'Classifying run.jsonl outcomes (harness-classification.json)...' -ForegroundColor DarkCyan
        try {
            & python $classifier --batch-root $batchRunsRoot
        }
        catch {
            try {
                & py -3 $classifier --batch-root $batchRunsRoot
            }
            catch {
                Write-Warning 'Could not run classify_harness_run_jsonl.py (install Python 3 or use tests/classify_harness_run_jsonl.py manually).'
            }
        }
    }
}

$exitHarness = 0
if (-not $DryRun) {
    $failed = if ($exitCode -ne 0) { 1 } else { 0 }
    if ($null -ne $Script:BatchEndBrief) {
        $b = $Script:BatchEndBrief
        Write-Host ''
        Write-Host '--- Run summary ---' -ForegroundColor Cyan
            Write-Host ("  Tool finishes: {0} | Tool failures: {1} | Blocker hints: {2} | Turns finished: {3}/{4}" -f $b.tool_finish_events, $b.tool_failures, $b.blocker_hints, $b.finished_turns, $b.expected_turns) -ForegroundColor Cyan
        Write-Host ("  Suites: {0} | Failed suites: {1} | Batch editor exit: {2} | All run.jsonl finished: {3} | Harness exit: {4} | All terminal: {5} | {6}s" -f $b.suites, $b.failed_suites, $b.batch_editor_exit, $b.all_jsonls_finished, $b.harness_exit, $b.all_terminal, $b.elapsed_sec) -ForegroundColor Cyan
    }
    else {
        Write-Host ("Completed suites={0} editor_exit={1} all_finished={2} failed_suites={3}" -f $allStatuses.Count, $exitCode, $dynamicDone, $failedSuiteCount) -ForegroundColor Cyan
    }
    if ($failed -gt 0) { $exitHarness = 1 }
}
else {
    Write-Host ("DryRun: suites prepared={0}" -f $allStatuses.Count) -ForegroundColor Cyan
}

if ($batchRunsRoot -and (Test-Path -LiteralPath $batchRunsRoot)) {
    try {
        $absRunDir = (Resolve-Path -LiteralPath $batchRunsRoot).Path
    }
    catch {
        $absRunDir = $batchRunsRoot
    }
    $absSummaryFile = Join-Path $absRunDir 'last-suite-summary.json'
    Write-Host ''
    Write-Host ("Batch output folder: {0}" -f $absRunDir) -ForegroundColor Green
    Write-Host ("last-suite-summary.json (full detail): {0}" -f $absSummaryFile) -ForegroundColor DarkGray
}

if ($exitHarness -ne 0) { exit $exitHarness }
exit 0
