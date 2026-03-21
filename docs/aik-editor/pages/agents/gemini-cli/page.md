# Gemini CLI Discord GitHub

- Source URL: https://aik.betide.studio/agents/gemini-cli

- Scraped At: 2026-03-20 15:49:03


## Text
Gemini CLI

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

Gemini CLI

Agents

Gemini CLI

Using Google's Gemini CLI as your AI agent

Copy Markdown Open

Gemini CLI provides access to Google's Gemini models with a generous free tier.

Video Tutorial

Setup

Install Gemini CLI:

npm install -g @google/gemini-cli

Authenticate using one of these methods:

Google Account - Run
gemini
and sign in when prompted

API Key - Set
GEMINI_API_KEY
environment variable

Vertex AI - Set
GOOGLE_CLOUD_PROJECT
and
GOOGLE_APPLICATION_CREDENTIALS

Configure the MCP server connection (see below)

Open the Agent Chat window in Unreal and select Gemini CLI from the agent dropdown

MCP Server Configuration

To give Gemini CLI access to Unreal Editor tools, you need to configure the MCP server connection.

Option 1: Via CLI Command (Recommended)

gemini mcp add unreal-editor http://localhost:9315/sse --transport sse

Option 2: Manual Configuration

Add the following to your
~/.gemini/settings.json
(Linux/macOS) or
%APPDATA%\gemini\settings.json
(Windows):

{ "mcpServers" : { "unreal-editor" : { "url" : "http://localhost:9315/sse" , "timeout" : 60000 , "description" : "Unreal Editor tools via AgentIntegrationKit" } } }

The
timeout
field (in milliseconds) is optional but recommended for tool calls that may take longer to execute.

The MCP server starts automatically when Unreal Editor launches with the plugin enabled. Make sure Unreal Editor is running before starting Gemini CLI.

Verify Connection

After configuration, start Gemini CLI and ask it to list available tools or run a safe read-only call like
read_asset
to verify the connection:

> What tools do you have access to from unreal-editor?

Features

Multiple Gemini models (2.5 Pro, 2.5 Flash, etc.)

Web search and URL context tools

Sandbox execution environment

MCP server support for Unreal Editor tools

Custom commands and extensions

Common unified Unreal tools exposed by this plugin include
edit_rigging
,
edit_animation_asset
,
edit_character_asset
,
edit_ai_tree
,
edit_graph
, and
generate_asset
.

Pricing

Gemini CLI offers a generous free tier through Google AI Studio. Check Google AI Studio for current limits.

Custom Path

If your Gemini CLI installation is in a non-standard location, set the path in Project Settings > Plugins > Agent Integration Kit > Gemini CLI Path .

Troubleshooting

Issue

Solution

MCP tools not available

Ensure Unreal Editor is running and the plugin is enabled

Connection refused

Check that the MCP server port (default 9315) is not blocked

Wrong port

Verify the port in Project Settings > Plugins > Agent Integration Kit > Server Port

Tools timing out

Increase
timeout
in settings.json (default is 30000ms)

Reset MCP Configuration

If you're having persistent connection issues, try removing and re-adding the server:

gemini mcp remove unreal-editor gemini mcp add unreal-editor http://localhost:9315/sse --transport sse

Debug Mode

Run Gemini CLI with debug logging to see detailed connection info:

gemini --log-level debug

Gemini CLI uses the SSE transport (Server-Sent Events). The plugin supports this via the
GET /sse
endpoint. Make sure your MCP configuration uses
http://localhost:9315/sse
as the URL.

Codex CLI

Using OpenAI's Codex CLI as your AI agent

Cursor

Using Cursor IDE with Unreal Editor tools via MCP

On this page

Video Tutorial Setup MCP Server Configuration Option 1: Via CLI Command (Recommended) Option 2: Manual Configuration Verify Connection Features Pricing Custom Path Troubleshooting Reset MCP Configuration Debug Mode
