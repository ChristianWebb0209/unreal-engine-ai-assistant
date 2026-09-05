# Smoke test for Unreal AI Editor MCP bridge (requires editor running with bridge enabled).
param(
    [int]$Port = 17777,
    [string]$TokenFile = "$env:LOCALAPPDATA\UnrealAiEditor\mcp\bridge.token",
    [switch]$HeroBlueprint
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $TokenFile)) {
    Write-Error "Token file not found: $TokenFile. Enable MCP Bridge in Project Settings and restart the editor."
}

$token = (Get-Content $TokenFile -Raw).Trim()
$headers = @{ Authorization = "Bearer $token" }
$base = "http://127.0.0.1:$Port"

function Invoke-BridgeTool {
    param([string]$ToolId, [hashtable]$Arguments = @{})
    $body = @{ tool_id = $ToolId; arguments = $Arguments } | ConvertTo-Json -Compress -Depth 12
    return Invoke-RestMethod -Uri "$base/v1/tools/invoke" -Method Post -Headers $headers -Body $body -ContentType 'application/json'
}

Write-Host "GET $base/health"
$health = Invoke-RestMethod -Uri "$base/health" -Headers $headers -Method Get
$health | ConvertTo-Json -Depth 6

Write-Host "GET $base/v1/tools"
$list = Invoke-RestMethod -Uri "$base/v1/tools" -Headers $headers -Method Get
Write-Host ("Tool count: {0} pack={1}" -f $list.tool_count, $list.pack_version)

Write-Host "POST editor_get_selection"
$sel = Invoke-BridgeTool -ToolId 'editor_get_selection'
$sel | ConvertTo-Json -Depth 8

Write-Host "POST asset_index_fuzzy_search (query=Blueprint)"
$assets = Invoke-BridgeTool -ToolId 'asset_index_fuzzy_search' -Arguments @{ query = 'Blueprint'; max_results = 5 }
$assets | ConvertTo-Json -Depth 8

if ($HeroBlueprint) {
    $bpPath = $null
    if ($assets.result -and $assets.result.matches) {
        $bpPath = $assets.result.matches[0].object_path
    }
    if (-not $bpPath) {
        Write-Warning 'HeroBlueprint: no blueprint path from search; skipping introspect/patch/compile.'
    } else {
        Write-Host "POST blueprint_graph_introspect ($bpPath)"
        $intro = Invoke-BridgeTool -ToolId 'blueprint_graph_introspect' -Arguments @{ blueprint_path = $bpPath }
        $intro | ConvertTo-Json -Depth 4

        Write-Host 'POST blueprint_compile'
        $compile = Invoke-BridgeTool -ToolId 'blueprint_compile' -Arguments @{ blueprint_path = $bpPath }
        $compile | ConvertTo-Json -Depth 6

        if (-not $compile.result.ok) {
            Write-Host 'POST engine_message_log_read'
            $log = Invoke-BridgeTool -ToolId 'engine_message_log_read' -Arguments @{ max_lines = 40 }
            $log | ConvertTo-Json -Depth 6
        }
    }
}

Write-Host 'OK: MCP bridge smoke passed.'
