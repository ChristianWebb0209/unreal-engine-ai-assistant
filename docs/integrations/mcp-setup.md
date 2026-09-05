# Unreal MCP setup (catalog-driven v1)

Connect **Cursor** (or any MCP client) to a running **Unreal Editor** with the **Unreal AI Editor** plugin. Tool **implementations** live in the merged catalog (`tools.main.json` + fragments); MCP only exposes an **allowlist** from [`mcp-tool-pack.v1.json`](../../Plugins/UnrealAiEditor/Resources/mcp/mcp-tool-pack.v1.json).

## Architecture

- **Plugin bridge:** localhost HTTP — `GET /health`, `GET /v1/tools`, `POST /v1/tools/invoke`
- **unreal-mcp (Node):** stdio MCP server; registers tools from `/v1/tools` at startup (fallback: `tools.generated.json`)
- **MCP tool names:** same as catalog `tool_id` (e.g. `blueprint_graph_patch`)

## 1. Editor

1. Build: `.\build-editor.ps1 -Headless`
2. Open project; enable plugin
3. **Edit → Project Settings → Plugins → Unreal AI Editor → MCP Bridge**
   - Enable **Enable MCP Bridge**
   - Port default **17777**
4. Output Log: `UnrealAi MCP Bridge: listening...` and token file path

## 2. Regenerate offline manifest (optional)

When the catalog or MCP pack changes:

```powershell
.\scripts\Generate-McpToolManifest.ps1
# or
.\scripts\Validate-McpToolPack.ps1
```

Updates:

- `unreal-mcp/resources/tools.generated.json`
- `Plugins/UnrealAiEditor/prompts/mcp/06-mcp-tool-pack-index.md`

## 3. Node MCP server

```powershell
cd unreal-mcp
npm install
npm run build
```

Environment (`unreal-mcp/.env.example`):

- `UNREAL_MCP_PORT` — match editor
- `UNREAL_MCP_TOKEN_FILE` — `%LOCALAPPDATA%\UnrealAiEditor\mcp\bridge.token`

```powershell
npm start
```

## 4. Cursor

See [integrations/cursor/mcp.example.json](../../integrations/cursor/mcp.example.json).

Add prompt chunks to project rules:

- `Plugins/UnrealAiEditor/prompts/mcp/00-identity.md` through `06-mcp-tool-pack-index.md`

## 5. Smoke test

```powershell
.\scripts\Test-McpBridge.ps1
```

## Hero scenario (Blueprint)

With bridge on and a known Character/AI Blueprint:

1. `asset_index_fuzzy_search` with `query`
2. `blueprint_graph_introspect` with `blueprint_path` from matches
3. `blueprint_graph_patch` (small test batch)
4. `blueprint_compile`
5. `engine_message_log_read` on failure

## Expanding tools

Edit `mcp-tool-pack.v1.json` only—then run `Generate-McpToolManifest.ps1`. No C++ dispatch changes required.
