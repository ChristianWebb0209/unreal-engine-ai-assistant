param(
    [string]$WorkspaceDslPath = "docs/architecture-maps/architecture.dsl",
    [string]$OutputDir = "docs/architecture-maps",
    [string]$ReadmePath = "README.md",
    [string]$CliPath = "",
    [string]$PlantUmlJarPath = ""
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

function Get-JarPath {
    param(
        [string]$Provided,
        [string[]]$Candidates,
        [string]$Label
    )
    if (-not [string]::IsNullOrWhiteSpace($Provided)) {
        return (Resolve-PathOrFail -PathValue $Provided -Label $Label)
    }
    foreach ($candidate in $Candidates) {
        $resolved = Resolve-Path -Path $candidate -ErrorAction SilentlyContinue
        if ($resolved) {
            return $resolved.Path
        }
    }
    throw "Could not locate $Label. Pass the explicit path."
}

function Ensure-Dir {
    param([string]$PathValue)
    if (-not (Test-Path $PathValue)) {
        New-Item -ItemType Directory -Path $PathValue | Out-Null
    }
}

function Clear-GeneratedFiles {
    param([string]$MapsDir)
    if (-not (Test-Path $MapsDir)) {
        return
    }
    $patterns = @("*.svg", "*.puml")
    foreach ($pattern in $patterns) {
        Get-ChildItem -Path $MapsDir -Filter $pattern -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    }
}

function To-Title {
    param([string]$Name)
    $title = $Name -replace '^structurizr-', ''
    $title = ($title -replace "[-_]+", " ")
    if ($title.Length -eq 0) { return $Name }
    return $title.Substring(0,1).ToUpper() + $title.Substring(1)
}

function Get-ViewOrderFromDsl {
    param([string]$DslPath)

    $raw = Get-Content -Path $DslPath -Raw
    $lines = $raw -split "`r?`n"
    $keys = New-Object System.Collections.Generic.List[string]

    foreach ($line in $lines) {
        # Match: systemContext plugin "system-context" ... or custom "mega-map" ...
        $m = [System.Text.RegularExpressions.Regex]::Match(
            $line,
            '^\s*(systemContext|container|component|dynamic|custom)\s+(?:\S+\s+)?"([^"]+)"'
        )
        if ($m.Success) {
            $key = $m.Groups[2].Value
            if (-not [string]::IsNullOrWhiteSpace($key) -and -not $keys.Contains($key)) {
                [void]$keys.Add($key)
            }
        }
    }

    return $keys
}

function Build-ReadmeArchitectureSection {
    param(
        [System.Collections.Generic.List[string]]$ViewOrder,
        [System.IO.FileInfo[]]$SvgFiles
    )

    $nl = [Environment]::NewLine
    $indexByKey = @{}
    for ($i = 0; $i -lt $ViewOrder.Count; $i++) {
        $indexByKey[$ViewOrder[$i]] = $i
    }
    $ordered = $SvgFiles | Where-Object { $_.Name -notlike "*-key.svg" } | Sort-Object `
        @{ Expression = {
                $k = $_.BaseName
                $k = $k -replace '^structurizr-', ''
                if ($indexByKey.ContainsKey($k)) { return $indexByKey[$k] }
                return 9999
           }}, `
        @{ Expression = { $_.Name } }

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append("<!-- ARCHITECTURE_MAPS_START -->$nl")
    [void]$sb.Append("## Architecture Maps$nl$nl")
    [void]$sb.Append("> **IMPORTANT:** Expand the section below to view all generated architecture diagrams (auto-generated from `docs/architecture-maps/architecture.dsl`).$nl$nl")
    [void]$sb.Append("<details>$nl")
    [void]$sb.Append("<summary><strong>▼ CLICK TO OPEN ARCHITECTURE GALLERY ▼</strong></summary>$nl$nl")

    foreach ($svg in $ordered) {
        $title = To-Title -Name $svg.BaseName
        [void]$sb.Append("### $title$nl$nl")
        [void]$sb.Append("[Open full-size SVG](docs/architecture-maps/$($svg.Name))$nl$nl")
        [void]$sb.Append("[![$title](docs/architecture-maps/$($svg.Name))](docs/architecture-maps/$($svg.Name))$nl$nl")
    }

    [void]$sb.Append("</details>$nl")
    [void]$sb.Append("<!-- ARCHITECTURE_MAPS_END -->$nl")
    return $sb.ToString()
}

function Upsert-ReadmeArchitectureSection {
    param(
        [string]$ReadmeFile,
        [string]$SectionContent
    )

    $nl = [Environment]::NewLine
    $content = Get-Content -Path $ReadmeFile -Raw
    $startMarker = "<!-- ARCHITECTURE_MAPS_START -->"
    $endMarker = "<!-- ARCHITECTURE_MAPS_END -->"

    if ($content.Contains($startMarker) -and $content.Contains($endMarker)) {
        $pattern = "(?s)<!-- ARCHITECTURE_MAPS_START -->.*?<!-- ARCHITECTURE_MAPS_END -->"
        $updated = [System.Text.RegularExpressions.Regex]::Replace($content, $pattern, $SectionContent.TrimEnd())
        Set-Content -Path $ReadmeFile -Value $updated -NoNewline
        return
    }

    $insertAnchor = "## Build and run (Windows, UE 5.7)"
    $anchorIndex = $content.IndexOf($insertAnchor)
    if ($anchorIndex -lt 0) {
        $updated = $content.TrimEnd() + $nl + $nl + $SectionContent.TrimEnd() + $nl
        Set-Content -Path $ReadmeFile -Value $updated -NoNewline
        return
    }

    $prefix = $content.Substring(0, $anchorIndex).TrimEnd()
    $suffix = $content.Substring($anchorIndex).TrimStart()
    $updated = $prefix + $nl + $nl + $SectionContent.TrimEnd() + $nl + $nl + $suffix
    Set-Content -Path $ReadmeFile -Value $updated -NoNewline
}

$workspaceDsl = Resolve-PathOrFail -PathValue $WorkspaceDslPath -Label "Workspace DSL"
$readme = Resolve-PathOrFail -PathValue $ReadmePath -Label "README"
$viewOrder = Get-ViewOrderFromDsl -DslPath $workspaceDsl
$cliPathResolved = $null
if (-not [string]::IsNullOrWhiteSpace($CliPath)) {
    $cliPathResolved = Resolve-PathOrFail -PathValue $CliPath -Label "Structurizr CLI executable"
} else {
    $candidates = @(
        "tools/structurizr-cli/structurizr.bat",
        "tools/structurizr-cli/structurizr.sh"
    )
    foreach ($candidate in $candidates) {
        $resolved = Resolve-Path -Path $candidate -ErrorAction SilentlyContinue
        if ($resolved) {
            $cliPathResolved = $resolved.Path
            break
        }
    }
    if (-not $cliPathResolved) {
        throw "Could not locate Structurizr CLI executable. Pass -CliPath explicitly."
    }
}
$plantUmlJar = Get-JarPath -Provided $PlantUmlJarPath -Candidates @("tools/plantuml/plantuml.jar", "plantuml.jar") -Label "PlantUML jar"

Ensure-Dir -PathValue $OutputDir
Clear-GeneratedFiles -MapsDir $OutputDir

Write-Host "Exporting PlantUML views from $workspaceDsl ..."
if ($cliPathResolved.ToLower().EndsWith(".sh")) {
    bash "$cliPathResolved" export -workspace "$workspaceDsl" -format plantuml -output "$OutputDir"
} else {
    & "$cliPathResolved" export -workspace "$workspaceDsl" -format plantuml -output "$OutputDir"
}

$pumlFiles = @(Get-ChildItem -Path $OutputDir -Filter *.puml -File -Recurse | Sort-Object Name)
if ($pumlFiles.Count -eq 0) {
    throw "No PlantUML files were exported to $OutputDir."
}

Write-Host "Rendering SVGs via PlantUML..."
foreach ($puml in $pumlFiles) {
    java -jar "$plantUmlJar" -tsvg "$($puml.FullName)"
}

$svgs = Get-ChildItem -Path $OutputDir -Filter *.svg -File | Sort-Object Name
if ($svgs.Count -eq 0) {
    throw "No SVG files were rendered in $OutputDir."
}

# Keep repository output clean: retain SVG artifacts only.
Get-ChildItem -Path $OutputDir -Filter *.puml -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

# Keep a root-level architecture.svg for quick linking.
$preferred = $svgs | Where-Object { $_.BaseName -eq "mega-map" } | Select-Object -First 1
if (-not $preferred) {
    $preferred = $svgs[0]
}
Copy-Item -Path $preferred.FullName -Destination "architecture.svg" -Force

$section = Build-ReadmeArchitectureSection -ViewOrder $viewOrder -SvgFiles $svgs
Upsert-ReadmeArchitectureSection -ReadmeFile $readme -SectionContent $section

Write-Host "Exported $($svgs.Count) architecture map(s)."
Write-Host "Updated README dropdown section and root architecture.svg."
