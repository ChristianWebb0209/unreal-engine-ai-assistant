# Codex CLI Discord GitHub

- Source URL: https://aik.betide.studio/agents/codex

- Scraped At: 2026-03-20 15:49:02


## Text
Codex CLI

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

Codex CLI

Agents

Codex CLI

Using OpenAI's Codex CLI as your AI agent

Copy Markdown Open

Codex CLI lets you use your OpenAI account with Agent Integration Kit.

You do not need to manually install
@zed-industries/codex-acp
. The ACP adapter binary ships with the plugin.

Setup

Install Codex CLI:

npm install -g @openai/codex

Authenticate with one of these methods:

ChatGPT subscription - Sign in when prompted (requires paid subscription)

API Key - Set
CODEX_API_KEY
or
OPENAI_API_KEY
environment variable

Run once in terminal to authenticate:

codex

Open the Agent Chat window in Unreal and select Codex CLI

Plugin Settings

Setting

Description

Codex CLI

Optional path override to
codex
executable (leave blank for auto-detect)

Bun/Node Runtime Override

Advanced override for adapter runtime; leave blank to use bundled runtime

Features

Context @-mentions

Tool calls with permission requests

TODO lists

Slash commands (
/review
,
/init
,
/compact
)

Custom prompts

Troubleshooting

Issue

Solution

Query closed before response received

Run
codex
in terminal and sign in again, then retry in Unreal

Codex not found

Run
codex --version
; if missing, install Codex CLI and set Codex CLI path override

Authentication issues

Run
codex
in terminal to re-authenticate

OpenRouter

Using OpenRouter as your AI agent

Gemini CLI

Using Google's Gemini CLI as your AI agent

On this page

Setup Plugin Settings Features Troubleshooting
