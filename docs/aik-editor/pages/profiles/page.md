# Profiles Discord GitHub

- Source URL: https://aik.betide.studio/profiles

- Scraped At: 2026-03-20 15:49:01


## Text
Profiles

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

Profiles

Profiles

Focus your AI agent on specific workflows by configuring tool access and custom instructions

Copy Markdown Open

Profiles let you control which tools an agent can see and provide domain-specific instructions. An agent using the Animation profile only sees animation-related tools and gets instructions tuned for motion matching, IK, and retargeting workflows.

Built-in Profiles

The plugin ships with 5 built-in profiles:

Profile

Focus

Key Tools

Full Toolkit

Everything enabled

All tools

Animation

Motion matching, IK, retargeting

Edit Blueprint (AnimGraphs), IK Rig, IK Retargeter, Pose Search, Montage

Blueprint & Gameplay

Game logic, systems

Blueprint, Behavior Trees, State Trees, Enhanced Input, Data Structures

Cinematics

Sequencing, cameras

Level Sequencer, Montage, Screenshots

VFX & Materials

Particles, shaders, procedural

Niagara, Material Graphs, PCG, Image Generation

Built-in profiles can be edited but not deleted.

Creating a Custom Profile

Open the Settings panel (gear icon in the chat window)

Click + New in the Profiles section

Give it a name and description

Select which tools to enable using the checkboxes

Add Custom Instructions to guide the agent's behavior

How Profiles Work

Tool Filtering

Profiles use a whitelist system:

Empty tool list = all tools enabled (this is how "Full Toolkit" works)

Non-empty tool list = only selected tools are available to the agent

The agent literally cannot see or call tools that aren't in the profile. This keeps it focused and reduces irrelevant tool calls.

Global tool disabling (in Project Settings) always takes priority over profiles. If a tool is globally disabled, no profile can re-enable it.

Custom Instructions

Each profile can include custom instructions that are appended to the system prompt. For example, the Animation profile includes guidance about motion matching workflows and Pose Search conventions.

Use this to tell the agent about your project's conventions, preferred patterns, or domain-specific knowledge.

Tool Description Overrides

Profiles can override individual tool descriptions to provide domain-specific context. For example, the Animation profile overrides
edit_blueprint
's description to emphasize AnimGraphs and State Machines rather than general Blueprint editing.

Selecting a Profile

Use the profile dropdown in the chat window header to switch profiles. The selected profile persists across editor sessions.

You cannot switch profiles during an active agent session. Finish or close the session first.

Chat Interface

Using the Agent Chat window

Troubleshooting

How to report crashes and help us fix them fast

On this page

Built-in Profiles Creating a Custom Profile How Profiles Work Tool Filtering Custom Instructions Tool Description Overrides Selecting a Profile
