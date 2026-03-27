param(
    [string]$WorkspaceDslPath = "docs/architecture-maps/architecture.dsl",
    [string]$OutputDir = "docs/architecture-maps",
    [string]$PagePath = "ARCHITECTURE_MAPS.md",
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
    $title = ($Name -replace "[-_]+", " ")
    if ($title.Length -eq 0) { return $Name }
    return $title.Substring(0,1).ToUpper() + $title.Substring(1)
}

function Write-ArchitecturePage {
    param(
        [string]$DestinationFile,
        [System.IO.FileInfo[]]$SvgFiles
    )

    $nl = [Environment]::NewLine
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append("# Architecture Maps$nl$nl")
    [void]$sb.Append('Generated from [`docs/architecture-maps/architecture.dsl`](docs/architecture-maps/architecture.dsl) via CI. This page is auto-written by `scripts/export-architecture-maps.ps1`.' + $nl + $nl)
    [void]$sb.Append('- Primary root image: [`architecture.svg`](architecture.svg)' + $nl)
    [void]$sb.Append('- Rendered view gallery folder: [`docs/architecture-maps/`](docs/architecture-maps/)' + $nl + $nl)

    foreach ($svg in $SvgFiles) {
        $title = To-Title -Name $svg.BaseName
        [void]$sb.Append("## $title$nl$nl")
        [void]$sb.Append("![$title](docs/architecture-maps/$($svg.Name))$nl$nl")
    }

    Set-Content -Path $DestinationFile -Value $sb.ToString() -NoNewline
}

$workspaceDsl = Resolve-PathOrFail -PathValue $WorkspaceDslPath -Label "Workspace DSL"
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

Write-ArchitecturePage -DestinationFile $PagePath -SvgFiles $svgs

Write-Host "Exported $($svgs.Count) architecture map(s)."
Write-Host "Updated docs architecture page and root architecture.svg."
