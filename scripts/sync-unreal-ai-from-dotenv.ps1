#requires -Version 5.1
<#
  Writes %LOCALAPPDATA%\UnrealAiEditor\settings\plugin_settings.json from OPENAI_* variables.

  Loads repo .env by default (via Import-RepoDotenv) so OPENAI_API_KEY / OPENAI_BASE_URL / OPENAI_MODEL
  match the rest of the tooling. Override the file with -EnvFile (applies that file with -Force for sync only).

  Usage (from repo root):
    .\scripts\sync-unreal-ai-from-dotenv.ps1
    .\scripts\sync-unreal-ai-from-dotenv.ps1 -EnvFile 'C:\path\other.env'
#>
[CmdletBinding()]
param(
    [string]$EnvFile = ''
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Import-RepoDotenv.ps1')

if ($EnvFile) {
    if (-not (Test-Path -LiteralPath $EnvFile)) {
        Write-Error "Env file not found: $EnvFile"
    }
    Import-RepoDotenv -Path $EnvFile -Force
}
else {
    Import-RepoDotenv -RepoRoot $RepoRoot
}

function JsonEscape([string]$s) {
    if ($null -eq $s) { return '' }
    return ($s -replace '\\', '\\' -replace '"', '\"' -replace "`n", '\n' -replace "`r", '\r' -replace "`t", '\t')
}

$key = if ($null -ne $env:OPENAI_API_KEY) { $env:OPENAI_API_KEY.Trim() } else { '' }
$base = if ($env:OPENAI_BASE_URL) { $env:OPENAI_BASE_URL.Trim() } else { 'https://api.openai.com/v1' }
$model = if ($env:OPENAI_MODEL) { $env:OPENAI_MODEL.Trim() } else { 'gpt-4o-mini' }

if ([string]::IsNullOrWhiteSpace($key)) {
    Write-Error 'OPENAI_API_KEY is empty. Set it in repo .env (see .env.example) or pass -EnvFile, then retry.'
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
Write-Host "Wrote $outPath (default model profile: $model)" -ForegroundColor Green
