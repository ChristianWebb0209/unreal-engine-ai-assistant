#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$OutputZip = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$PluginsRoot = Join-Path $RepoRoot "Plugins"
$AiPlugin = Join-Path $PluginsRoot "UnrealAiEditor"
$FormatterPlugin = Join-Path $PluginsRoot "UnrealBlueprintFormatter"
$DistDir = Join-Path $RepoRoot "dist"

if (-not (Test-Path $AiPlugin)) {
    throw "Missing plugin folder: $AiPlugin"
}
if (-not (Test-Path $FormatterPlugin)) {
    throw "Missing plugin folder: $FormatterPlugin"
}

if ([string]::IsNullOrWhiteSpace($OutputZip)) {
    $OutputZip = Join-Path $DistDir "UnrealAiEditor-bundled-plugins.zip"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputZip)) {
    $OutputZip = Join-Path $RepoRoot $OutputZip
}

$Staging = Join-Path $DistDir "plugin-bundle-staging"

if (Test-Path $Staging) {
    Remove-Item $Staging -Recurse -Force
}
New-Item -ItemType Directory -Path $Staging | Out-Null
New-Item -ItemType Directory -Path (Join-Path $Staging "Plugins") | Out-Null

function Copy-PluginForBundle {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePluginDir,
        [Parameter(Mandatory = $true)][string]$DestPluginsRoot
    )
    $Name = Split-Path $SourcePluginDir -Leaf
    $Dest = Join-Path $DestPluginsRoot $Name
    Copy-Item -Path $SourcePluginDir -Destination $Dest -Recurse -Force

    $Prune = @("Binaries", "Intermediate", ".git")
    foreach ($Rel in $Prune) {
        $P = Join-Path $Dest $Rel
        if (Test-Path $P) {
            Remove-Item $P -Recurse -Force
        }
    }

    Get-ChildItem -Path $Dest -Recurse -File | Where-Object {
        $_.Name -eq ".DS_Store" -or
        $_.Name -eq "Thumbs.db" -or
        $_.Name -like "*.old" -or
        $_.Name -like "*.obj" -or
        $_.Name -like "*.pdb" -or
        $_.Name -like "*.dep.json" -or
        $_.Name -like "*.sarif"
    } | Remove-Item -Force
}

$DestPluginsRoot = Join-Path $Staging "Plugins"
Copy-PluginForBundle -SourcePluginDir $AiPlugin -DestPluginsRoot $DestPluginsRoot
Copy-PluginForBundle -SourcePluginDir $FormatterPlugin -DestPluginsRoot $DestPluginsRoot

if (-not (Test-Path $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir | Out-Null
}
if (Test-Path $OutputZip) {
    Remove-Item $OutputZip -Force
}

$PythonZip = @"
import os
import sys
import zipfile

staging = sys.argv[1]
out_zip = sys.argv[2]
with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_DEFLATED) as zf:
    for root, _, files in os.walk(staging):
        for name in files:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, staging)
            zf.write(full, rel)
"@

$TempPy = Join-Path $DistDir "zip_bundle_tmp.py"
Set-Content -Path $TempPy -Value $PythonZip -Encoding UTF8
try {
    python "$TempPy" "$Staging" "$OutputZip"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create bundle zip via python."
    }
}
finally {
    if (Test-Path $TempPy) {
        Remove-Item $TempPy -Force
    }
}

Remove-Item $Staging -Recurse -Force

Write-Host "Bundled plugin zip created:" -ForegroundColor Green
Write-Host $OutputZip
