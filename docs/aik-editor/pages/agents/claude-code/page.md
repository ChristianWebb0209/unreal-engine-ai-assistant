# Claude Code Discord GitHub

- Source URL: https://aik.betide.studio/agents/claude-code

- Scraped At: 2026-03-20 15:49:01


## Text
Claude Code

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

Claude Code

Agents

Claude Code

Claude Code is Anthropic's official agentic coding tool and one of the best agents for programming tasks.

Copy Markdown Open

Claude Code is the default suggested agent for NeoStack AI. In current stage, you almost get ~2000$ worth of API usage with a Claude Max subscription which is only 200$ per month. This makes using models like Opus 4.5 which are BEAST for coding tasks extremely cost effective.

Video Tutorial

Prerequisites

A Claude subscription (Pro, Max, Teams, or Enterprise) or Claude Console account

Agent Integration Kit plugin installed

You do not need to manually install
@zed-industries/claude-code-acp
anymore. The ACP adapter is bundled with the plugin.

Step 1: Install Claude Code

macOS/Linux/WSL Windows PowerShell Windows CMD Homebrew WinGet

curl -fsSL https://claude.ai/install.sh | bash

irm https: // claude.ai / install.ps1 | iex

curl -fsSL https://claude.ai/install.cmd -o install.cmd && install.cmd && del install.cmd

brew install --cask claude-code

winget install Anthropic.ClaudeCode

Verify installation:
claude --version

Step 2: Authenticate Claude Code

Before using Claude Code with the plugin, you must authenticate it first. Open a terminal and run:

claude

Follow the prompts to log in. Authentication methods:

Claude Max (recommended) - Best value, ~$2000 worth of API usage for $200/month

Claude Pro - Use your Claude.ai subscription

API Key - For console.anthropic.com users

You only need to authenticate once. Claude Code will remember your credentials.

Step 3: Use Claude Code in Unreal

Open the Agent Chat window in Unreal and select Claude Code .

Plugin Settings

Setting

Description

Claude Code CLI

Optional path override to
claude
executable (leave blank for auto-detect)

Bun/Node Runtime Override

Advanced override for adapter runtime; leave blank to use bundled runtime

Troubleshooting

Issue

Solution

Query closed before response received

Run
claude
in terminal and complete login again, then retry in Unreal

Claude not found

Run
claude --version
; if missing, install Claude Code and set Claude Code CLI path override

Authentication issues

Run
claude
in terminal to re-authenticate

No response from agent

Check the in-app auth banner and complete sign-in flow

Choosing an Agent

Which AI agent and subscription plan should you use with Agent Integration Kit?

OpenRouter

Using OpenRouter as your AI agent

On this page

Video Tutorial Prerequisites Step 1: Install Claude Code Step 2: Authenticate Claude Code Step 3: Use Claude Code in Unreal Plugin Settings Troubleshooting
