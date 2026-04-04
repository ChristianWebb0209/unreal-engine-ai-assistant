#requires -Version 5.1
<#
.SYNOPSIS
  Deletes Unreal AI Editor local chat threads and memory store; keeps API keys (plugin_settings.json).

.DESCRIPTION
  Data root: %LOCALAPPDATA%\UnrealAiEditor (see FUnrealAiPersistenceStub::GetLocalDataRoot).

  REMOVES (recursive):
    - chats\       (per-project threads, conversation.json, context.json, etc.)
    - memories\    (memory index, items, tombstones)

  PRESERVES:
    - settings\plugin_settings.json  (providers, models, API keys)
    - settings\usage_stats.json      (optional; left intact)
    - vector\                        (retrieval index; not removed unless -IncludeVector)

  Note: Tool names like blueprint_graph_patch still appear in assembled *system* prompts from
  the plugin repo (prompts/chunks). Clearing chats does not change that—only thread history.

.PARAMETER IncludeVector
  Also remove vector\<project_id>\ SQLite retrieval index (rebuilds on next open if enabled).

.PARAMETER WhatIf
  Show what would be deleted without removing.

.EXAMPLE
  .\scripts\Clear-UnrealAiEditor-ChatsAndMemories.ps1
  .\scripts\Clear-UnrealAiEditor-ChatsAndMemories.ps1 -IncludeVector
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$IncludeVector
)

$ErrorActionPreference = 'Stop'

$Root = Join-Path $env:LOCALAPPDATA 'UnrealAiEditor'
if (-not (Test-Path -LiteralPath $Root)) {
    Write-Host "Nothing to do: data root does not exist: $Root" -ForegroundColor Yellow
    exit 0
}

$SettingsFile = Join-Path $Root 'settings\plugin_settings.json'
if (-not (Test-Path -LiteralPath $SettingsFile)) {
    Write-Warning "Expected settings file missing (API keys may be elsewhere): $SettingsFile"
} else {
    Write-Host "Preserving: $SettingsFile" -ForegroundColor Green
}

$targets = @(
    @{ Path = Join-Path $Root 'chats';    Label = 'chat threads' },
    @{ Path = Join-Path $Root 'memories'; Label = 'memories' }
)
if ($IncludeVector) {
    $targets += @{ Path = Join-Path $Root 'vector'; Label = 'vector retrieval index' }
}

foreach ($t in $targets) {
    $p = $t.Path
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Host "Skip (not present): $p" -ForegroundColor DarkGray
        continue
    }
    if ($PSCmdlet.ShouldProcess($p, "Remove $($t.Label)")) {
        Remove-Item -LiteralPath $p -Recurse -Force
        Write-Host "Removed: $p" -ForegroundColor Cyan
    }
}

Write-Host "Clear-UnrealAiEditor-ChatsAndMemories: done. Root: $Root" -ForegroundColor Green
