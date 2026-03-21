# Quick Start Discord GitHub

- Source URL: https://aik.betide.studio/getting-started/quick-start

- Scraped At: 2026-03-20 15:49:00


## Text
Quick Start

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

Quick Start

Getting Started

Quick Start

Get up and running with Agent Integration Kit in 5 minutes

Copy Markdown Open

1. Open the Chat Window

Go to Tools > Agent Chat or use the keyboard shortcut to open the chat window.

2. Connect to an Agent

Option A: Claude Code (Recommended)

Claude Max subscription ($200/month) gives you ~$2000 worth of API usage - making powerful models like Opus 4.5 extremely cost effective for development.

Install Claude Code: See Claude Code Setup for installation instructions

Authenticate by running
claude
in terminal

Install ACP adapter:
npm install -g @zed-industries/claude-code-acp

Select Claude Code from the agent dropdown and click Connect

Option B: OpenRouter

Get an API key from openrouter.ai

In Project Settings, paste your key in OpenRouter API Key

Select OpenRouter from the agent dropdown in the chat window

Click Connect

3. Start Prompting

Once connected, type a prompt and press Enter:

Create a Blueprint Actor called BP_Coin with: - A StaticMeshComponent called Mesh - A SphereComponent called CollisionSphere - A float variable called PointValue set to 10

4. Review Tool Calls

The AI will execute tools to create your Blueprint. You'll see:

Tool calls with their arguments

Permission requests for destructive operations

Results showing what was created

5. Attach Context

For more accurate results, attach context to your prompts:

Right-click a Blueprint node in the graph editor

Select Attach to Agent Prompt

The node context will be included in your next message

Example Workflows

Create a Health System

Create a Blueprint Component called BPC_HealthComponent with: - Float variable MaxHealth = 100 - Float variable CurrentHealth = 100 - Event dispatcher OnHealthChanged - Event dispatcher OnDeath - Function TakeDamage(float Amount) that reduces CurrentHealth and broadcasts events

Modify Existing Blueprint

Open BP_PlayerCharacter and: - Add a SpringArm component attached to root - Add a Camera component attached to the spring arm - Set SpringArmLength to 300

Create AI Behavior

Create a Behavior Tree called BT_PatrolAndChase with: - Selector at root - Sequence for chasing (check if player visible, move to player) - Sequence for patrolling (move to random point, wait 2 seconds)

Configuration

Configure agents and settings for Agent Integration Kit

Agents

AI agents supported by Agent Integration Kit

On this page

1. Open the Chat Window 2. Connect to an Agent Option A: Claude Code (Recommended) Option B: OpenRouter 3. Start Prompting 4. Review Tool Calls 5. Attach Context Example Workflows Create a Health System Modify Existing Blueprint Create AI Behavior
