#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Suite,
    [string]$EngineRoot = '',
    [string]$ProjectRoot = '',
    [int]$MaxSuites = 0,
    [switch]$DryRun,
    [switch]$KeepTempSuites
)

$ErrorActionPreference = 'Stop'

# Strict headed harness is implemented by reusing the qualitative headed executor,
# but with:
# - assertions supported via `assertions[]` in each turn
# - runs redirected into `tests/strict-tests/runs/`
#
# This wrapper intentionally bypasses the older telemetry-based strict evaluation logic below.
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

$code = 0
& $longRunner @runnerParams
$code = $LASTEXITCODE
exit $code

function Resolve-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}

function Resolve-ScenarioFolder {
    param(
        [string]$RepoRoot,
        [string]$PathLike
    )
    if ([string]::IsNullOrWhiteSpace($PathLike)) {
        throw 'ScenarioFolder is required.'
    }
    if ([System.IO.Path]::IsPathRooted($PathLike)) {
        return (Resolve-Path -LiteralPath $PathLike).Path
    }
    $candidate = Join-Path $RepoRoot $PathLike
    if (Test-Path -LiteralPath $candidate) {
        return (Resolve-Path -LiteralPath $candidate).Path
    }
    throw "ScenarioFolder not found: $PathLike"
}

function Get-LatestBatchFolder {
    param([string]$RunsRoot)
    $dirs = Get-ChildItem -LiteralPath $RunsRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^run-(\d+)-' } |
        Sort-Object { [int]([regex]::Match($_.Name, '^run-(\d+)-').Groups[1].Value) } -Descending
    if (-not $dirs -or $dirs.Count -eq 0) {
        return $null
    }
    return $dirs[0].FullName
}

function Get-RunJsonlEvents {
    param([string]$RunJsonlPath)
    $events = @()
    if (-not (Test-Path -LiteralPath $RunJsonlPath)) {
        return $events
    }
    $lines = Get-Content -LiteralPath $RunJsonlPath -Encoding UTF8 -ErrorAction SilentlyContinue
    foreach ($line in $lines) {
        $trim = [string]$line
        if ([string]::IsNullOrWhiteSpace($trim)) {
            continue
        }
        try {
            $events += ($trim | ConvertFrom-Json)
        }
        catch {
        }
    }
    return $events
}

function Get-TurnDerivedStats {
    param([object[]]$Events)
    $toolCalledByName = @{}
    $toolSucceededByName = @{}
    $toolFailedByName = @{}
    $eventCountByType = @{}
    $assistantText = ''
    $runFinishedSuccess = $null

    foreach ($e in $Events) {
        if (-not $e -or -not $e.type) {
            continue
        }
        $type = [string]$e.type
        if (-not $eventCountByType.ContainsKey($type)) {
            $eventCountByType[$type] = 0
        }
        $eventCountByType[$type] = [int]$eventCountByType[$type] + 1

        if ($type -eq 'assistant_delta' -and $null -ne $e.chunk) {
            $assistantText += [string]$e.chunk
        }
        elseif ($type -eq 'tool_finish') {
            $tool = [string]$e.tool
            if (-not [string]::IsNullOrWhiteSpace($tool)) {
                if (-not $toolCalledByName.ContainsKey($tool)) { $toolCalledByName[$tool] = 0 }
                $toolCalledByName[$tool] = [int]$toolCalledByName[$tool] + 1
                if ($e.success -eq $true) {
                    if (-not $toolSucceededByName.ContainsKey($tool)) { $toolSucceededByName[$tool] = 0 }
                    $toolSucceededByName[$tool] = [int]$toolSucceededByName[$tool] + 1
                }
                else {
                    if (-not $toolFailedByName.ContainsKey($tool)) { $toolFailedByName[$tool] = 0 }
                    $toolFailedByName[$tool] = [int]$toolFailedByName[$tool] + 1
                }
            }
        }
        elseif ($type -eq 'run_finished') {
            $runFinishedSuccess = [bool]$e.success
        }
    }

    return [ordered]@{
        assistant_text = $assistantText
        run_finished_success = $runFinishedSuccess
        tool_called = $toolCalledByName
        tool_succeeded = $toolSucceededByName
        tool_failed = $toolFailedByName
        event_count = $eventCountByType
    }
}

function Get-MapCount {
    param($Map, [string]$Key)
    if ($null -eq $Map -or [string]::IsNullOrWhiteSpace($Key)) { return 0 }
    if ($Map.Contains($Key)) { return [int]$Map[$Key] }
    return 0
}

function Evaluate-Assertion {
    param(
        [hashtable]$Stats,
        [pscustomobject]$Assertion
    )
    $atype = [string]$Assertion.type
    if ([string]::IsNullOrWhiteSpace($atype)) {
        return [ordered]@{ pass = $false; message = 'assertion.type is required' }
    }

    switch ($atype) {
        'run_finished_success' {
            $expected = $true
            if ($null -ne $Assertion.equals) { $expected = [bool]$Assertion.equals }
            $actual = $Stats.run_finished_success
            $ok = ($null -ne $actual) -and ($actual -eq $expected)
            return [ordered]@{
                pass = $ok
                message = "run_finished_success expected=$expected actual=$actual"
            }
        }
        'tool_called' {
            $tool = [string]$Assertion.tool
            $min = if ($null -ne $Assertion.min_count) { [int]$Assertion.min_count } else { 1 }
            $actual = Get-MapCount -Map $Stats.tool_called -Key $tool
            $ok = $actual -ge $min
            return [ordered]@{
                pass = $ok
                message = "tool_called tool=$tool min=$min actual=$actual"
            }
        }
        'tool_succeeded' {
            $tool = [string]$Assertion.tool
            $min = if ($null -ne $Assertion.min_count) { [int]$Assertion.min_count } else { 1 }
            $actual = Get-MapCount -Map $Stats.tool_succeeded -Key $tool
            $ok = $actual -ge $min
            return [ordered]@{
                pass = $ok
                message = "tool_succeeded tool=$tool min=$min actual=$actual"
            }
        }
        'tool_failed_max' {
            $tool = [string]$Assertion.tool
            $max = if ($null -ne $Assertion.max_count) { [int]$Assertion.max_count } else { 0 }
            $actual = Get-MapCount -Map $Stats.tool_failed -Key $tool
            $ok = $actual -le $max
            return [ordered]@{
                pass = $ok
                message = "tool_failed_max tool=$tool max=$max actual=$actual"
            }
        }
        'event_count_min' {
            $etype = [string]$Assertion.event_type
            $min = [int]$Assertion.min_count
            $actual = Get-MapCount -Map $Stats.event_count -Key $etype
            $ok = $actual -ge $min
            return [ordered]@{
                pass = $ok
                message = "event_count_min type=$etype min=$min actual=$actual"
            }
        }
        'event_count_max' {
            $etype = [string]$Assertion.event_type
            $max = [int]$Assertion.max_count
            $actual = Get-MapCount -Map $Stats.event_count -Key $etype
            $ok = $actual -le $max
            return [ordered]@{
                pass = $ok
                message = "event_count_max type=$etype max=$max actual=$actual"
            }
        }
        'assistant_contains' {
            $needle = [string]$Assertion.text
            $hay = [string]$Stats.assistant_text
            $ok = -not [string]::IsNullOrWhiteSpace($needle) -and $hay.ToLowerInvariant().Contains($needle.ToLowerInvariant())
            return [ordered]@{
                pass = $ok
                message = "assistant_contains text='$needle'"
            }
        }
        'assistant_not_contains' {
            $needle = [string]$Assertion.text
            $hay = [string]$Stats.assistant_text
            $ok = [string]::IsNullOrWhiteSpace($needle) -or (-not $hay.ToLowerInvariant().Contains($needle.ToLowerInvariant()))
            return [ordered]@{
                pass = $ok
                message = "assistant_not_contains text='$needle'"
            }
        }
        default {
            return [ordered]@{
                pass = $false
                message = "Unsupported assertion type '$atype'"
            }
        }
    }
}

function New-TempLongRunningSuiteFromStrict {
    param(
        [string]$StrictSuitePath,
        [string]$TempRoot
    )
    $raw = Get-Content -LiteralPath $StrictSuitePath -Raw -Encoding UTF8
    $suite = $raw | ConvertFrom-Json
    $turns = @($suite.turns)
    if ($turns.Count -eq 0) {
        throw "Strict suite has no turns: $StrictSuitePath"
    }

    $out = [ordered]@{
        suite_id = if ($suite.suite_id) { [string]$suite.suite_id } else { [System.IO.Path]::GetFileNameWithoutExtension($StrictSuitePath) }
        default_type = if ($suite.default_type) { [string]$suite.default_type } else { 'agent' }
        turns = @()
    }
    foreach ($t in $turns) {
        $out.turns += [ordered]@{
            type = if ($t.type) { [string]$t.type } else { $out.default_type }
            request = [string]$t.request
        }
    }

    $slug = [System.IO.Path]::GetFileNameWithoutExtension($StrictSuitePath)
    $suiteDir = Join-Path $TempRoot $slug
    New-Item -ItemType Directory -Force -Path $suiteDir | Out-Null
    $outPath = Join-Path $suiteDir 'suite.json'
    ($out | ConvertTo-Json -Depth 12) + "`n" | Set-Content -LiteralPath $outPath -Encoding UTF8
    return $outPath
}

$repoRoot = Resolve-RepoRoot
$strictScenarioAbs = Resolve-ScenarioFolder -RepoRoot $repoRoot -PathLike $ScenarioFolder
$longRunner = Join-Path $repoRoot 'tests\qualitative-tests\run-qualitative-headed.ps1'
$runsRoot = Join-Path $repoRoot 'tests\qualitative-tests\runs'

if (-not (Test-Path -LiteralPath $longRunner)) {
    throw "Missing long-running harness runner: $longRunner"
}
if (-not (Test-Path -LiteralPath $runsRoot)) {
    New-Item -ItemType Directory -Force -Path $runsRoot | Out-Null
}

$strictSuiteFiles = @(
    Get-ChildItem -LiteralPath $strictScenarioAbs -Recurse -File -Filter $SuiteFileName -ErrorAction Stop |
        Sort-Object FullName
)
if ($strictSuiteFiles.Count -eq 0) {
    throw "No strict suite file '$SuiteFileName' found under $strictScenarioAbs"
}
if ($MaxSuites -ne 0) {
    $take = [Math]::Min([Math]::Abs($MaxSuites), $strictSuiteFiles.Count)
    $strictSuiteFiles = @($strictSuiteFiles[0..($take - 1)])
}

$tempRoot = Join-Path $repoRoot ("tests\strict-tests\_tmp_strict_run_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

$strictFailures = @()
$strictResults = @()

try {
    foreach ($strictSuiteFile in $strictSuiteFiles) {
        Write-Host ("[strict] Running suite: {0}" -f $strictSuiteFile.FullName) -ForegroundColor Cyan
        $beforeLatest = Get-LatestBatchFolder -RunsRoot $runsRoot
        $generatedSuitePath = New-TempLongRunningSuiteFromStrict -StrictSuitePath $strictSuiteFile.FullName -TempRoot $tempRoot
        $generatedScenarioDir = Split-Path -Parent $generatedSuitePath

        $runnerArgs = @{
            ScenarioFolder = $generatedScenarioDir
            SuiteFileName = 'suite.json'
            MaxSuites = 1
        }
        if (-not [string]::IsNullOrWhiteSpace($EngineRoot)) { $runnerArgs.EngineRoot = $EngineRoot }
        if (-not [string]::IsNullOrWhiteSpace($ProjectRoot)) { $runnerArgs.ProjectRoot = $ProjectRoot }
        if ($DryRun) { $runnerArgs.DryRun = $true }

        & $longRunner @runnerArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Underlying headed harness failed for strict suite: $($strictSuiteFile.FullName)"
        }

        $afterLatest = Get-LatestBatchFolder -RunsRoot $runsRoot
        if ([string]::IsNullOrWhiteSpace($afterLatest) -or $afterLatest -eq $beforeLatest) {
            throw "Could not detect new batch folder after strict suite run: $($strictSuiteFile.FullName)"
        }

        $summaryPath = Join-Path $afterLatest 'last-suite-summary.json'
        if (-not (Test-Path -LiteralPath $summaryPath)) {
            throw "Missing last-suite-summary.json: $summaryPath"
        }
        $strictSuite = (Get-Content -LiteralPath $strictSuiteFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json)
        $longSummary = (Get-Content -LiteralPath $summaryPath -Raw -Encoding UTF8 | ConvertFrom-Json)

        if ($longSummary.files_processed -lt 1 -or @($longSummary.runs).Count -lt 1) {
            throw "Unexpected summary shape in: $summaryPath"
        }
        $suiteRun = @($longSummary.runs)[0]
        $strictTurns = @($strictSuite.turns)
        $runTurns = @($suiteRun.turns)
        if ($strictTurns.Count -ne $runTurns.Count) {
            throw "Turn count mismatch strict=$($strictTurns.Count) run=$($runTurns.Count) for suite $($strictSuiteFile.FullName)"
        }

        $suiteResult = [ordered]@{
            strict_suite = $strictSuiteFile.FullName
            batch_folder = $afterLatest
            pass = $true
            dry_run = [bool]$DryRun
            turns = @()
        }

        if ($DryRun) {
            for ($i = 0; $i -lt $strictTurns.Count; $i++) {
                $strictTurn = $strictTurns[$i]
                $runTurn = $runTurns[$i]
                $runJsonl = Join-Path ([string]$runTurn.step_dir) 'run.jsonl'
                $suiteResult.turns += [ordered]@{
                    turn_index = $i + 1
                    turn_id = if ($strictTurn.id) { [string]$strictTurn.id } else { "turn_$('{0:D2}' -f ($i + 1))" }
                    request = [string]$strictTurn.request
                    run_jsonl = $runJsonl
                    pass = $true
                    skipped = $true
                    assertions = @(
                        [ordered]@{
                            type = 'dry_run_skip'
                            pass = $true
                            message = 'Assertions skipped in dry run mode.'
                        }
                    )
                }
            }
            $strictResults += $suiteResult
            continue
        }

        for ($i = 0; $i -lt $strictTurns.Count; $i++) {
            $strictTurn = $strictTurns[$i]
            $runTurn = $runTurns[$i]
            $runJsonl = Join-Path ([string]$runTurn.step_dir) 'run.jsonl'
            $events = Get-RunJsonlEvents -RunJsonlPath $runJsonl
            $stats = Get-TurnDerivedStats -Events $events

            $assertions = @($strictTurn.assertions)
            if ($assertions.Count -eq 0) {
                $assertions = @([pscustomobject]@{ type = 'run_finished_success'; equals = $true })
            }
            $assertionResults = @()
            $turnPass = $true
            foreach ($a in $assertions) {
                $res = Evaluate-Assertion -Stats $stats -Assertion $a
                $assertionResults += [ordered]@{
                    type = [string]$a.type
                    pass = [bool]$res.pass
                    message = [string]$res.message
                }
                if (-not [bool]$res.pass) {
                    $turnPass = $false
                    $suiteResult.pass = $false
                    $strictFailures += [ordered]@{
                        strict_suite = $strictSuiteFile.FullName
                        batch_folder = $afterLatest
                        turn_index = $i + 1
                        turn_id = if ($strictTurn.id) { [string]$strictTurn.id } else { "turn_$('{0:D2}' -f ($i + 1))" }
                        request = [string]$strictTurn.request
                        assertion = [string]$a.type
                        failure = [string]$res.message
                        run_jsonl = $runJsonl
                    }
                }
            }

            $suiteResult.turns += [ordered]@{
                turn_index = $i + 1
                turn_id = if ($strictTurn.id) { [string]$strictTurn.id } else { "turn_$('{0:D2}' -f ($i + 1))" }
                request = [string]$strictTurn.request
                run_jsonl = $runJsonl
                pass = $turnPass
                assertions = $assertionResults
            }
        }

        $strictResults += $suiteResult
    }
}
finally {
    if (-not $KeepTempSuites -and (Test-Path -LiteralPath $tempRoot)) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$summaryOut = [ordered]@{
    strict_scenario_folder = $strictScenarioAbs
    suites_processed = $strictResults.Count
    failed_assertions = $strictFailures.Count
    pass = ($strictFailures.Count -eq 0)
    generated_utc = (Get-Date).ToUniversalTime().ToString('o')
    suites = $strictResults
}

$latestBatch = Get-LatestBatchFolder -RunsRoot $runsRoot
if ($null -ne $latestBatch) {
    $strictSummaryPath = Join-Path $latestBatch 'strict-summary.json'
    $strictFailuresPath = Join-Path $latestBatch 'strict-failures.json'
    ($summaryOut | ConvertTo-Json -Depth 20) + "`n" | Set-Content -LiteralPath $strictSummaryPath -Encoding UTF8
    ($strictFailures | ConvertTo-Json -Depth 20) + "`n" | Set-Content -LiteralPath $strictFailuresPath -Encoding UTF8
    Write-Host ("[strict] Summary: {0}" -f $strictSummaryPath) -ForegroundColor Green
    Write-Host ("[strict] Failures: {0}" -f $strictFailuresPath) -ForegroundColor DarkGray
}

if ($strictFailures.Count -gt 0) {
    Write-Error ("Strict assertions failed: {0}" -f $strictFailures.Count)
}

Write-Host '[strict] All assertions passed.' -ForegroundColor Green
