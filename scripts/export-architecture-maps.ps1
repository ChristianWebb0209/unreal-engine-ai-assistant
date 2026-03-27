param(
    [string]$WorkspaceDslPath = "docs/architecture-maps/architecture.dsl",
    [string]$OutputDir = "docs/architecture-maps",
    [string]$ReadmePath = "README.md",
    [string]$CliJarPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-PathOrFail {
    param([string]$PathValue, [string]$Label)
    $resolved = Resolve-Path -Path $PathValue -ErrorAction SilentlyContinue
    if (-not $resolved) {
        throw "$Label not found: $PathValue"
    }
    return $resolved.Path
}

function New-OrClearDir {
    param([string]$PathValue)
    if (Test-Path $PathValue) {
        Get-ChildItem -Path $PathValue -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    } else {
        New-Item -ItemType Directory -Path $PathValue | Out-Null
    }
}

function Get-StructurizrJarPath {
    param([string]$Provided)
    if (-not [string]::IsNullOrWhiteSpace($Provided)) {
        return (Resolve-PathOrFail -PathValue $Provided -Label "Structurizr CLI jar")
    }

    $candidates = @(
        "tools/structurizr-cli/structurizr-cli.jar",
        "structurizr-cli.jar"
    )

    foreach ($candidate in $candidates) {
        $resolved = Resolve-Path -Path $candidate -ErrorAction SilentlyContinue
        if ($resolved) {
            return $resolved.Path
        }
    }

    throw "Could not locate structurizr-cli.jar. Pass -CliJarPath explicitly."
}

function Build-ArchitectureSection {
    param(
        [string]$MapsDirRelative,
        [System.IO.FileInfo[]]$SvgFiles
    )

    $nl = [Environment]::NewLine
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append("## Architecture Maps$nl$nl")
    [void]$sb.Append("- Primary map for quick scanning: [`architecture.svg`](architecture.svg)$nl")
    [void]$sb.Append("- Full rendered gallery: [`docs/architecture-maps/`](docs/architecture-maps)$nl")
    [void]$sb.Append("- Source of truth: [`docs/architecture-maps/architecture.dsl`](docs/architecture-maps/architecture.dsl)$nl$nl")
    [void]$sb.Append("This section is generated from `docs/architecture-maps/architecture.dsl` by CI. Each view below is rendered as SVG from Structurizr DSL and committed to the repo for easy GitHub rendering.$nl$nl")
    [void]$sb.Append("<!-- ARCHITECTURE_MAPS_START -->$nl")

    foreach ($svg in $SvgFiles) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($svg.Name)
        $title = ($name -replace "[-_]+", " ")
        if ($title.Length -gt 0) {
            $title = ($title.Substring(0,1).ToUpper() + $title.Substring(1))
        }
        [void]$sb.Append("### $title$nl$nl")
        [void]$sb.Append("![${title}]($MapsDirRelative/$($svg.Name))$nl$nl")
    }

    [void]$sb.Append("<!-- ARCHITECTURE_MAPS_END -->$nl")
    return $sb.ToString()
}

function Upsert-ArchitectureSection {
    param(
        [string]$ReadmeFile,
        [string]$SectionContent
    )

    $content = Get-Content -Path $ReadmeFile -Raw
    $startMarker = "<!-- ARCHITECTURE_MAPS_START -->"
    $endMarker = "<!-- ARCHITECTURE_MAPS_END -->"

    if ($content.Contains($startMarker) -and $content.Contains($endMarker)) {
        $pattern = "(?s)## Architecture Maps.*?$endMarker"
        $replacement = $SectionContent.TrimEnd()
        $updated = [System.Text.RegularExpressions.Regex]::Replace($content, $pattern, $replacement)
        Set-Content -Path $ReadmeFile -Value $updated -NoNewline
        return
    }

    # Insert early in README (after title + intro paragraph if possible).
    $insertAfter = $content.IndexOf([Environment]::NewLine + "## ")
    if ($insertAfter -lt 0) {
        $updatedContent = $content.TrimEnd() + [Environment]::NewLine + [Environment]::NewLine + $SectionContent.TrimEnd() + [Environment]::NewLine
        Set-Content -Path $ReadmeFile -Value $updatedContent -NoNewline
        return
    }

    $prefix = $content.Substring(0, $insertAfter).TrimEnd()
    $suffix = $content.Substring($insertAfter).TrimStart()
    $updated = $prefix + [Environment]::NewLine + [Environment]::NewLine + $SectionContent.TrimEnd() + [Environment]::NewLine + [Environment]::NewLine + $suffix
    Set-Content -Path $ReadmeFile -Value $updated -NoNewline
}

$workspaceDsl = Resolve-PathOrFail -PathValue $WorkspaceDslPath -Label "Workspace DSL"
$readme = Resolve-PathOrFail -PathValue $ReadmePath -Label "README"
$jar = Get-StructurizrJarPath -Provided $CliJarPath

New-OrClearDir -PathValue $OutputDir

Write-Host "Exporting SVG views from $workspaceDsl ..."
java -jar "$jar" export -workspace "$workspaceDsl" -format svg -output "$OutputDir"

$svgs = Get-ChildItem -Path $OutputDir -Filter *.svg -File | Sort-Object Name
if ($svgs.Count -eq 0) {
    throw "No SVG files were exported to $OutputDir."
}

# Keep a root-level architecture.svg for README/quick preview.
$preferred = $svgs | Where-Object { $_.BaseName -eq "mega-map" } | Select-Object -First 1
if (-not $preferred) {
    $preferred = $svgs[0]
}
Copy-Item -Path $preferred.FullName -Destination "architecture.svg" -Force

$section = Build-ArchitectureSection -MapsDirRelative "docs/architecture-maps" -SvgFiles $svgs
Upsert-ArchitectureSection -ReadmeFile $readme -SectionContent $section

Write-Host "Exported $($svgs.Count) architecture map(s)."
Write-Host "Updated README architecture section and root architecture.svg."
