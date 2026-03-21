# Configuration Discord GitHub

- Source URL: https://aik.betide.studio/getting-started/configuration

- Scraped At: 2026-03-20 15:49:00


## Text
Configuration

NeoStack AI

NeoStack AI
Search
⌘ K

Agent Integration Kit
Documentation

Getting Started
Installation Configuration Quick Start

Agents

Chat Interface Profiles
Resources
Troubleshooting Comparison Changelog

Discord Discord GitHub GitHub

Betide Studio

Configuration

Getting Started

Configuration

Configure agents and settings for Agent Integration Kit

Copy Markdown Open

Project Settings

Open Edit > Project Settings > Plugins > Agent Integration Kit to configure the plugin.

Agent Configuration

Setting

Description

Default Agent

Which agent to use when opening the chat window

Auto Connect

Automatically connect to the default agent on startup

Verbose Logging

Enable detailed ACP protocol logging for debugging

OpenRouter Settings

Setting

Description

API Key

Your OpenRouter API key (required for OpenRouter agent)

Default Model

Model to use by default (e.g.,
anthropic/claude-sonnet-4
)

External Agent Paths

Setting

Description

Claude Code Path

Path to
claude-code-acp
executable (leave blank if in PATH)

Node.js Path (Windows)

Path to
node.exe
for Windows users

Agent Profiles

You can save multiple agent configurations as profiles:

Configure an agent's settings

Click Save Profile

Enter a profile name

Switch between profiles from the dropdown

Each profile stores:

Agent type

API keys

Model selection

Environment variables

Environment Variables

Some agents require environment variables. Set them in the Environment Variables section:

ANTHROPIC_API_KEY=sk-ant-... OPENROUTER_API_KEY=sk-or-...

These are passed to external agent processes when launched.

MCP Server Settings

Setting

Description

Server Port

HTTP port for MCP server (default: 9315)

Auto Start

Start MCP server when editor launches

Supported Transport Protocols

The MCP server supports two transport protocols for maximum compatibility:

Protocol

Specification

Endpoints

Clients

Streamable HTTP

MCP 2025-03-26

POST /mcp
,
GET /mcp

Gemini CLI, newer clients

HTTP+SSE

MCP 2024-11-05

GET /sse
,
POST /message

Claude Code, legacy clients

Both protocols are served simultaneously on the same port. Clients are automatically detected based on their connection method.

Installation

How to install Agent Integration Kit in your Unreal Engine project

Quick Start

Get up and running with Agent Integration Kit in 5 minutes

On this page

Project Settings Agent Configuration OpenRouter Settings External Agent Paths Agent Profiles Environment Variables MCP Server Settings Supported Transport Protocols
