#requires -Version 5.1
<#
  Validates MCP tool pack ids exist in merged catalog with parameters schemas.
  Run from repo root: .\scripts\Validate-McpToolPack.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Py = Join-Path $RepoRoot 'scripts\generate_mcp_tools.py'
& python $Py --validate-only
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'Validate-McpToolPack: OK'
