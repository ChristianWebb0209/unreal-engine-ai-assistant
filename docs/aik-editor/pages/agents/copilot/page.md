# Copilot CLI Discord GitHub

- Source URL: https://aik.betide.studio/agents/copilot

- Scraped At: 2026-03-20 15:49:02


## Text
Copilot CLI

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

Copilot CLI

Agents

Copilot CLI

Using GitHub Copilot CLI as your AI agent

Copy Markdown Open

GitHub Copilot CLI brings GitHub's Copilot agent directly to the command line, with native ACP support for seamless integration with Agent Integration Kit.

Prerequisites

A GitHub Copilot subscription (Individual, Business, or Enterprise)

Node.js 22+ (if installing via npm)

Step 1: Install Copilot CLI

npm Homebrew WinGet Install Script

npm install -g @github/copilot

brew install copilot-cli

winget install GitHub.Copilot

curl -fsSL https://gh.io/copilot-install | bash

Verify installation:
copilot --version

Step 2: Authenticate

After installing, you must log in to your GitHub account. Open a terminal and run:

copilot login

Follow the on-screen instructions to authenticate with your GitHub account.

You must run
copilot login
at least once before using it in the plugin. The plugin spawns Copilot non-interactively, so it cannot handle the login prompt.

Step 3: Use in Unreal Editor

Open the Agent Chat window in Unreal Editor

Select Copilot CLI from the agent dropdown

Start chatting — the plugin will automatically spawn Copilot in ACP mode with access to all Unreal Editor tools

The plugin automatically writes the MCP server configuration to
~/.copilot/mcp-config.json
on first launch, giving Copilot access to all Unreal Editor tools.

If automatic configuration doesn't work, you can manually add the MCP server. Open Copilot CLI in a terminal and run:

/mcp add

Then configure with these values:

Name :
unreal-editor

Type :
http

URL :
http://localhost:9315/mcp

Tools :
*

Replace
9315
with the actual MCP server port shown in the Output Log.

Plugin Settings

Setting

Description

Copilot CLI

Path to
copilot
executable (leave blank if in PATH)

Node.js Path (Windows)

Path to
node.exe
— required on Windows if Node.js is not in PATH (npm installs only)

Settings are in Project Settings > Plugins > Agent Integration Kit > Path Overrides .

Features

GitHub Copilot's agentic capabilities inside Unreal Editor

Native ACP support — no separate adapter needed

Streaming responses with tool call support

Permission requests for tool execution

Multiple model support

Troubleshooting

Issue

Solution

"Copilot CLI not found"

Run
copilot --version
in terminal. If not found, reinstall or set the full path in settings

"Node.js not found" (Windows)

Set Node.js Path in Project Settings (only applies to npm installs)

Authentication issues

Run
copilot
in terminal and use
/login
to re-authenticate

No response from agent

Check Output Log for pipe errors. Ensure no other process is using the Copilot CLI

Connection timeout

Restart the editor and try again. Verify your Copilot subscription is active

Cursor

Using Cursor IDE with Unreal Editor tools via MCP

Chat Interface

Using the Agent Chat window

On this page

Prerequisites Step 1: Install Copilot CLI Step 2: Authenticate Step 3: Use in Unreal Editor Plugin Settings Features Troubleshooting
