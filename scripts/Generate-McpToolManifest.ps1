#requires -Version 5.1
<#
  Validates mcp-tool-pack.v1.json against merged tool catalog and writes
  unreal-mcp/resources/tools.generated.json plus prompts/mcp/06-mcp-tool-pack-index.md
  Run from repo root: .\scripts\Generate-McpToolManifest.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Py = Join-Path $RepoRoot 'scripts\generate_mcp_tools.py'
if (-not (Test-Path -LiteralPath $Py)) {
    Write-Error "Missing $Py"
}
& python $Py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'Generate-McpToolManifest: OK'
