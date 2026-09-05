# Unreal MCP — bridge prerequisites

Before calling Unreal tools:

1. **Unreal Editor** is running with the **Unreal AI Editor** plugin enabled.
2. **MCP Bridge** is on: **Edit → Project Settings → Plugins → Unreal AI Editor → MCP Bridge → Enable MCP Bridge**.
3. Note the **port** (default `17777`) and **token file** path logged when the bridge starts (`%LOCALAPPDATA%\UnrealAiEditor\mcp\bridge.token` on Windows).
4. The **unreal-mcp** Node server must use the same port and token (`UNREAL_MCP_PORT`, `UNREAL_MCP_TOKEN_FILE`).
5. Verify connectivity: `GET http://127.0.0.1:<port>/health` with header `Authorization: Bearer <token>`.

If health fails, fix bridge settings before retrying tool calls.
