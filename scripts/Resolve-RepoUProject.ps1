#requires -Version 5.1
<#
  Resolve the Unreal project manifest (.uproject) for this repo's test/sample layout.

  Convention
  - For local development and CI, we assume a single UE project lives at the repository root
    (alongside Plugins/UnrealAiEditor). The basename of that file defines the editor target
    (<BaseName>Editor) used by build-editor.ps1.

  Optional override
  - Set UE_REPO_UPROJECT to a path relative to the repo root or an absolute path when multiple
    .uproject files exist at the root or you keep the manifest outside the default layout.
#>
function Resolve-RepoUProject {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    $RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path.TrimEnd('\', '/')

    $envPath = [string]$env:UE_REPO_UPROJECT
    if (-not [string]::IsNullOrWhiteSpace($envPath)) {
        $full = if ([System.IO.Path]::IsPathRooted($envPath)) {
            $envPath
        } else {
            Join-Path $RepoRoot $envPath
        }
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
            Write-Error "UE_REPO_UPROJECT is set but the file was not found: $full"
        }
        $item = Get-Item -LiteralPath $full
        return [pscustomobject]@{
            FullPath     = $item.FullName
            BaseName     = $item.BaseName
            EditorTarget = ('{0}Editor' -f $item.BaseName)
        }
    }

    $candidates = @(
        Get-ChildItem -LiteralPath $RepoRoot -Filter '*.uproject' -File -ErrorAction SilentlyContinue |
            Sort-Object Name
    )
    if ($candidates.Count -eq 0) {
        Write-Error @"
No .uproject found in repo root: $RepoRoot

Add a UE project at the repository root (see README), or set UE_REPO_UPROJECT to the manifest path
(relative to the repo root or absolute).
"@
    }
    if ($candidates.Count -gt 1) {
        $listed = ($candidates | ForEach-Object { $_.Name }) -join ', '
        Write-Error @"
Multiple .uproject files at repo root: $listed

Keep a single test project at the root, or set UE_REPO_UPROJECT to the one you want to build and open.
"@
    }

    $u = $candidates[0]
    return [pscustomobject]@{
        FullPath     = $u.FullName
        BaseName     = $u.BaseName
        EditorTarget = ('{0}Editor' -f $u.BaseName)
    }
}
