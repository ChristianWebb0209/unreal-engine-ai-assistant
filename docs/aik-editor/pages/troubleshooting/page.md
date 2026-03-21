# Troubleshooting Discord GitHub

- Source URL: https://aik.betide.studio/troubleshooting

- Scraped At: 2026-03-20 15:49:01


## Text
Troubleshooting

NeoStack AI

NeoStack AI
Search
⌘ K

Agent Integration Kit
Documentation

Getting Started

Agents

Chat Interface Profiles
Resources
Troubleshooting Comparison Changelog

Discord Discord GitHub GitHub

Betide Studio

Troubleshooting

Troubleshooting

How to report crashes and help us fix them fast

Copy Markdown Open

Reporting a Crash

Agent Integration Kit logs every AI operation automatically. If you experience a crash, sending us this log helps us fix the issue quickly.

Send Us These Files

File

Location

Tool Audit Log

Saved/Logs/AIK_ToolAudit.log

Engine Log

Saved/Logs/<YourProject>.log

How to find them: Open your project folder →
Saved
→
Logs
. Or from Unreal: File → Open Project Folder then go into
Saved/Logs/
.

Send both files to us on Discord along with:

What you were asking the AI to do when it crashed

Your Unreal Engine version (5.5, 5.6, 5.7)

Which agent you were using (Claude Code, Gemini CLI, Codex, OpenRouter)

What the Audit Log Contains

Every AI action is recorded — which asset was modified, what changed, and whether it succeeded or failed:

[2026-02-08 14:23:01] OK | edit_blueprint | BP_Player | + Variable: Health (Float) [2026-02-08 14:23:02] OK | configure_asset | BP_Player | bReplicates: false -> true [2026-02-08 14:23:03] FAIL | edit_blueprint | BP_Enemy | Function 'OnDeath' not found

This lets us trace exactly what the AI did before the crash and reproduce the issue on our end.

Profiles

Focus your AI agent on specific workflows by configuring tool access and custom instructions

Comparison

See how Agent Integration Kit compares to every other AI tool for Unreal Engine.

On this page

Reporting a Crash Send Us These Files What the Audit Log Contains
