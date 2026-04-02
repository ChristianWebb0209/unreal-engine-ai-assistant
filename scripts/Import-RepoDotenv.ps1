#requires -Version 5.1
<#
  Loads KEY=VALUE pairs from a .env file into the current process environment.
  Intended keys: UE_ENGINE_ROOT, optional UE_REPO_UPROJECT, OPENAI_* (see .env.example).

  - Lines starting with # or empty lines are skipped.
  - Optional leading "export " is stripped.
  - First "=" separates key from value; values may be quoted with " or '.

  Usage (at top of another script's body, after param binding):
    . (Join-Path $PSScriptRoot 'scripts\Import-RepoDotenv.ps1')
    Import-RepoDotenv -RepoRoot $RepoRoot

  Does not override variables already set in the environment unless -Force is used.
#>
function Import-RepoDotenv {
    [CmdletBinding()]
    param(
        [string]$RepoRoot = '',
        [string]$Path = '',
        [switch]$Force
    )

    if ($Path -and (Test-Path -LiteralPath $Path)) {
        $envPath = $Path
    }
    elseif ($RepoRoot) {
        $envPath = Join-Path $RepoRoot '.env'
    }
    else {
        return
    }

    if (-not (Test-Path -LiteralPath $envPath)) {
      return
    }

    Get-Content -LiteralPath $envPath -Encoding UTF8 | ForEach-Object {
        $line = $_.Trim()
        if ($line -eq '' -or $line.StartsWith('#')) {
            return
        }
        if ($line.StartsWith('export ')) {
            $line = $line.Substring(7).Trim()
        }
        $eq = $line.IndexOf('=')
        if ($eq -lt 1) {
            return
        }
        $k = $line.Substring(0, $eq).Trim()
        $v = $line.Substring($eq + 1).Trim()
        if ($v.StartsWith('"') -and $v.EndsWith('"') -and $v.Length -ge 2) {
            $v = $v.Substring(1, $v.Length - 2)
        }
        elseif ($v.StartsWith("'") -and $v.EndsWith("'") -and $v.Length -ge 2) {
            $v = $v.Substring(1, $v.Length - 2)
        }
        $v = $v.Trim()
        if ([string]::IsNullOrWhiteSpace($k)) {
            return
        }
        if (-not $Force) {
            $existing = [Environment]::GetEnvironmentVariable($k, 'Process')
            if ($null -ne $existing -and $existing -ne '') {
                return
            }
        }
        Set-Item -Path "Env:$k" -Value $v
    }
}
