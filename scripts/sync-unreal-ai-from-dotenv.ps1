#requires -Version 5.1
<#
  Reads OPENAI_API_KEY, OPENAI_BASE_URL, OPENAI_MODEL from a .env file (e.g. godot-llm/.env)
  and writes %LOCALAPPDATA%\UnrealAiEditor\settings\plugin_settings.json for Unreal AI Editor.

  Usage:
    .\scripts\sync-unreal-ai-from-dotenv.ps1 -EnvFile 'C:\Github\godot-llm\.env'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EnvFile
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $EnvFile)) {
    Write-Error "Env file not found: $EnvFile"
}

function JsonEscape([string]$s) {
    if ($null -eq $s) { return '' }
    return ($s -replace '\\', '\\' -replace '"', '\"' -replace "`n", '\n' -replace "`r", '\r' -replace "`t", '\t')
}

$kv = @{}
Get-Content -LiteralPath $EnvFile -Encoding UTF8 | ForEach-Object {
    $line = $_.Trim()
    if ($line -eq '' -or $line.StartsWith('#')) { return }
    $eq = $line.IndexOf('=')
    if ($eq -lt 1) { return }
    $k = $line.Substring(0, $eq).Trim()
    $v = $line.Substring($eq + 1).Trim()
    $kv[$k] = $v
}

$key = $kv['OPENAI_API_KEY']
$base = if ($kv['OPENAI_BASE_URL']) { $kv['OPENAI_BASE_URL'] } else { 'https://api.openai.com/v1' }
$model = if ($kv['OPENAI_MODEL']) { $kv['OPENAI_MODEL'] } else { 'gpt-4o-mini' }

if ([string]::IsNullOrWhiteSpace($key)) {
    Write-Error 'OPENAI_API_KEY missing in .env'
}

$outDir = Join-Path $env:LOCALAPPDATA 'UnrealAiEditor\settings'
$outPath = Join-Path $outDir 'plugin_settings.json'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$jk = JsonEscape $key
$jb = JsonEscape $base
$jm = JsonEscape $model

$json = @"
{
  "version": 3,
  "api": {
    "baseUrl": "$jb",
    "apiKey": "$jk",
    "defaultModel": "$jm",
    "defaultProviderId": ""
  },
  "providers": [],
  "models": {
    "$jm": {
      "providerId": "",
      "modelIdForApi": "$jm",
      "maxContextTokens": 128000,
      "maxOutputTokens": 4096,
      "supportsNativeTools": true,
      "supportsParallelToolCalls": true
    }
  }
}
"@

Set-Content -LiteralPath $outPath -Value $json -Encoding UTF8
Write-Host "Wrote $outPath (default model profile: $model)"
