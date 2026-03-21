# Cursor Discord GitHub

- Source URL: https://aik.betide.studio/agents/cursor

- Scraped At: 2026-03-20 15:49:02


## Text
Cursor

NeoStack AI

NeoStack AI
Search
⌘ K

Agent Integration Kit
Documentation

Getting Started

Agents
Choosing an Agent Claude Code OpenRouter Codex CLI Gemini CLI Cursor Copilot CLI

Chat Interface Profiles
Resources
Troubleshooting Comparison Changelog

Discord Discord GitHub GitHub

Betide Studio

Cursor

Agents

Cursor

Using Cursor IDE with Unreal Editor tools via MCP

Copy Markdown Open

Cursor does not have an ACP (Agent Client Protocol) adapter, which means you cannot use Cursor directly inside the Unreal Editor chat window. Instead, you must use Cursor's own interface and connect to Unreal via MCP.

Why We Recommend Claude Code Instead

Using Cursor with MCP means you lose several features compared to agents with full ACP support:

No integrated chat - You have to switch between Unreal Editor and Cursor

No automatic context - Cursor won't automatically know what you're working on in the editor

No image attachments - Can't easily share viewport screenshots or UI references

Limited streaming - Response streaming may not be as smooth

Claude Code with a Claude Max subscription (~$200/month) gives you approximately $2000 worth of API usage - that's multiple times more than what you get with Cursor Pro, and it runs significantly faster with full ACP integration.

Still Want to Use Cursor?

If you need to use Cursor, you can connect it to Unreal Editor's MCP server to access all the editor tools.

Prerequisites

Cursor installed

Unreal Editor running with the plugin enabled

Configuration

Create a file at
.cursor/mcp.json
in your Unreal project folder:

{ "mcpServers" : { "unreal-editor" : { "url" : "http://localhost:9315/mcp" } } }

Or for global access (all projects), create
~/.cursor/mcp.json
:

Windows macOS/Linux

C:\Users\<username>\.cursor\mcp.json

~/.cursor/mcp.json

{ "mcpServers" : { "unreal-editor" : { "url" : "http://localhost:9315/mcp" } } }

Verify Connection

Make sure Unreal Editor is running (the MCP server starts automatically on port 9315)

Restart Cursor or reload the window

In Cursor's chat, check that Unreal Editor tools appear under "Available Tools"

Test the connection by asking Cursor:

What tools do you have from unreal-editor?

Or:

Use read_asset on /Game to test the Unreal Editor connection

Available Tools

Once connected, Cursor can use all the Unreal Editor tools including:

read_asset - Read and inspect Unreal assets

edit_blueprint - Blueprint editing plus Enhanced Input routing (
asset_domain='enhanced_input'
)

edit_graph - Edit graphs and search for available nodes

edit_ai_tree - Unified BehaviorTree/StateTree editing

edit_rigging - Unified motion stack + Control Rig editing

edit_animation_asset - Unified montage/anim sequence/blend space editing

edit_character_asset - Unified skeleton/physics-asset editing

generate_asset - Unified image + 3D generation

And many more tools from the 15-tool surface...

Troubleshooting

Issue

Solution

Tools not showing

Ensure Unreal Editor is running before starting Cursor

Connection refused

Check that port 9315 is not blocked by firewall

Server not found

Verify the plugin is enabled in your project

The MCP server runs on
http://localhost:9315/mcp
by default. You can change the port in Project Settings > Plugins > Agent Integration Kit > Server Port .

Gemini CLI

Using Google's Gemini CLI as your AI agent

Copilot CLI

Using GitHub Copilot CLI as your AI agent

On this page

Why We Recommend Claude Code Instead Still Want to Use Cursor? Prerequisites Configuration Verify Connection Available Tools Troubleshooting
