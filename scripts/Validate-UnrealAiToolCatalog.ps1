#requires -Version 5.1
<#
  Structural validation for Plugins/UnrealAiEditor/Resources/UnrealAiToolCatalog.json.
  Fails the build/CI if JSON is invalid, tool_count mismatches, or tool_id duplicates exist.
  Run from repo root: .\scripts\Validate-UnrealAiToolCatalog.ps1
#>
[CmdletBinding()]
param(
    [string]$CatalogPath = ''
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($CatalogPath)) {
    $CatalogPath = Join-Path $RepoRoot 'Plugins\UnrealAiEditor\Resources\UnrealAiToolCatalog.json'
}

if (-not (Test-Path -LiteralPath $CatalogPath)) {
    Write-Error "Catalog not found: $CatalogPath"
}

$raw = Get-Content -LiteralPath $CatalogPath -Raw -Encoding UTF8
try {
    $doc = $raw | ConvertFrom-Json
} catch {
    Write-Error "Invalid JSON: $_"
    exit 1
}

if (-not $doc.meta) {
    Write-Error 'Catalog missing meta object.'
    exit 1
}

$v = $doc.meta.resolver_contract.version
if ([string]::IsNullOrWhiteSpace($v)) {
    Write-Error 'meta.resolver_contract.version is required.'
    exit 1
}

if (-not $doc.tools -or $doc.tools -isnot [System.Collections.IEnumerable]) {
    Write-Error 'Catalog missing tools array.'
    exit 1
}

$tools = @($doc.tools)
$count = $tools.Count
if ($doc.meta.tool_count -ne $count) {
    Write-Error "meta.tool_count ($($doc.meta.tool_count)) does not match tools array length ($count)."
    exit 1
}

$seen = @{}
foreach ($t in $tools) {
    if (-not $t.tool_id) {
        Write-Error 'Tool entry missing tool_id.'
        exit 1
    }
    $id = [string]$t.tool_id
    if ($seen.ContainsKey($id)) {
        Write-Error "Duplicate tool_id: $id"
        exit 1
    }
    $seen[$id] = $true
    if (-not $t.parameters) {
        Write-Error "Tool $id missing parameters object."
        exit 1
    }
}

Write-Host "Validate-UnrealAiToolCatalog: OK ($count tools, resolver_contract $v)." -ForegroundColor Green
exit 0
