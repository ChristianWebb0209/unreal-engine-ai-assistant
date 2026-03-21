# Changelog Discord GitHub

- Source URL: https://aik.betide.studio/changelog

- Scraped At: 2026-03-20 15:48:59


## Text
Changelog

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

Changelog

Changelog

Release history for Agent Integration Kit (NeoStack AI)

Copy Markdown Open

v0.5.6 — February 21, 2026

Focused on making agent setup and installation more reliable — fewer manual steps, better error recovery.

What's Better

In-process Claude installer — Claude Code can now install directly without opening a separate terminal

Automatic Bun lockfile recovery — corrupted
bun install
states are detected and fixed automatically

Unix binary permissions handled — macOS quarantine clearing, chmod, and executable verification happen automatically

Bundled adapters copy to writable directory — no more issues with read-only build folders

Reasoning effort controls are more reliable — properly handles agents with non-standard config identifiers

Full keyboard navigation in dialogs — arrow keys, Tab, and Enter work in permission and question prompts

Better OpenRouter setup messaging — clearer error states for missing keys, credit hints, install commands with copy button

Other Improvements

Session deletion correctly resolves native vs. Unreal session IDs

Settings reorganized with clearer "Agent Process Overrides" section

File attachment handling centralized and improved

Claude Code and Codex agent docs updated

v0.5.5 — February 18, 2026

Focused polish update after v0.5.3 — better day-to-day reliability, smoother agent setup, and fewer “stuck” workflows.

What’s Better

More reliable editing in UE 5.5 — improved stability when working with advanced rigging/VFX assets

Major agent stability upgrade for GitHub Copilot CLI + Gemini CLI — significantly improved startup/session reliability and fewer connection/model issues

Fewer broken agent starts overall — session startup and model selection are now more dependable across workflows

Setup flow is smoother — restart action is now available directly from setup when needed

Update checks are easier — added manual
Check for Updates
inside Settings → About

Typing in chat behaves better — improved non-English/IME input behavior and keyboard handling while chatting

External links open correctly — popups now open in your system browser instead of creating awkward in-editor windows

Stability Improvements

Reduced edge-case session hangs and tool dead-ends

Improved backend request/session handling to keep long-running agent workflows stable

v0.5.0 — February 16, 2026

Our biggest update yet — new UI, smarter agents, zero-setup install, and you can now continue conversations across different agents.

Completely New UI

The chat interface has been rebuilt from the ground up. Everything is faster, cleaner, and more capable.

Onboarding wizard — new users get a guided setup flow instead of figuring things out manually

Session sidebar — browse, resume, and manage all your chat sessions in a collapsible sidebar

Rich asset previews — when the agent reads an asset, results are now rendered with color-coded sections by asset type instead of raw text

See what the agent is planning — a dedicated plan panel shows the agent's thinking and steps as it works

Source control at a glance — see your current Git branch, pending changes, and submit directly from the chat window

Browse and search models — pick from all available models with a searchable browser and descriptions

Control reasoning depth — adjust how much the agent thinks before responding (off / low / medium / high / max)

Live usage tracking — see token counts, session cost, and how much of the context window is used in real time

UI updates without plugin rebuilds — interface improvements ship independently from plugin versions

Agents Are Smarter with Fewer Tools

We consolidated 27+ tools down to about 15. Instead of the agent choosing between
edit_ik_rig
,
edit_ik_retargeter
,
edit_pose_search
,
edit_control_rig
, and
edit_enhanced_input
(and sometimes picking the wrong one), there's now a single
edit_rigging
tool that figures out what you mean automatically.

This applies across the board — animation assets, AI trees, character assets, screenshots, and generation tools are all unified. The result: agents make fewer mistakes, pick the right tool more often, and work faster.

Agent Can See What You See

The screenshot tool is now much more powerful. By default, it auto-captures whatever viewport you're focused on — whether that's your level, a material editor, a skeletal mesh preview, or a Niagara system. The agent can also control the camera: move to a specific location, focus on an actor, orbit around an asset, or switch view modes — all to understand your scene better.

Zero-Setup Install

Everything the plugin needs to launch agents is now bundled. No more installing Node.js, running npm, or debugging PATH issues.

Claude Code and Codex adapters ship with the plugin, ready to go on Windows, Mac, and Linux

Existing manual installs are still detected and work fine

Continue Conversations Across Agents

Start a task in Claude Code, then pick it up in Codex or OpenRouter — without losing context.

Chat handoff — the plugin generates an AI summary of your conversation that the next agent can understand

Load Claude Code history — resume any past Claude Code session directly in the plugin

Kimi CLI added as a new supported agent

Environment Query System (EQS) Support

Agents can now create and edit EQS assets — add query nodes, configure tests and generators, and wire up AI perception queries through natural language.

Much More Stable

Tool execution is now wrapped in crash protection — if something goes wrong, the editor stays alive and the operation is rolled back cleanly

The plugin now validates Blueprint health after every edit, catching corruption before it causes problems later

Threading issues that could cause random crashes with MCP agents are eliminated

Other Improvements

See per-model cost breakdowns (input, output, cache, web search) in the usage display

Plugin can now update itself directly from Betide Studio, so you don't have to wait for Fab marketplace review

New Choosing an Agent guide to help you pick the right agent for your workflow

Fixed OpenRouter sessions hanging indefinitely

Fixed Copilot CLI failing to start

Fixed build errors on some configurations

Fixed markdown rendering issues in chat

v0.4.0 — February 10, 2026

Animation Tools — 100% Coverage

BlendSpace editing — create/edit 1D and 2D blend spaces, add/remove/edit samples, configure axis parameters

AnimSequence editing — bone track keyframing (add/update/remove keys with position, rotation, scale), notifies, curves, sync markers, additive settings, root motion

Skeleton editing — sockets, virtual bones, retarget settings, anim slots, curve metadata, blend profiles

Physics Asset editing — full ragdoll setup: auto-generate from skeletal mesh, add/remove bodies with collision shapes (sphere, box, capsule, tapered capsule), constraints with angular limits (swing1, swing2, twist), physics types, collision pairs, weld bodies

Linked Anim Layers & Anim Layer Interfaces — agents can build modular animation rigs with upper/lower body separation, the last missing piece for full motion matching pipelines

AnimGraph layer editing — new
animlayer:
graph selector lets agents edit animation layer function graphs directly

Improvements

Bone track keyframe readback — agents can now read back actual keyframe data they generate, enabling verification and iteration instead of working blind

Paste images in the Input Box — attach images directly from clipboard

Control Rig variable editing fixed

Minor Behavior Tree adjustments

v0.3.2 — February 9, 2026

100% Control Rig support — both the bone/control hierarchy and the RigVM graph

Behavior Tree tool rewritten — almost 100% rebuilt to be more dynamic

Much better State Trees tool — improved across the board

Agents can now one-shot tasks — significant prompting improvements (ongoing)

3D Model Generation via Meshy — fixed all previous issues, animation and rigging support coming soon

Meshy usage shown in Usage Panel

Preparations for the next big update covering 100% of Unreal's NPC features

UI improved — agent status is now properly shown

Export chat sessions as Markdown

Various tools made smoother to use

v0.3.0 — February 8, 2026

Our biggest update yet, again!

Blueprint Editing — 100% Coverage

Composite Graphs, Comments, Macros, Local Variables, Components, Reparenting, and Blueprint Interfaces are now supported

Pin Splitting/Recombining & Dynamic Exec Pins

Class Defaults editing — ConfigureAssetTool can now set Actor CDO properties like
bReplicates
,
NetUpdateFrequency
,
AutoPossessPlayer

Stability

Almost 500+ additional checks added across the plugin to reduce crashes

Fixed packaging issues when using as a plugin

Fixed issues with MCP port conflicts

New Features

Agent Usage Limits — See OpenAI Codex and Claude Code usage limits directly in the plugin

Task Completion Notifications — Toasts, taskbar flash, and sound playback when the agent finishes (configurable)

GitHub Copilot Native CLI support — Docs

Codex Reasoning parameter now supported

Subconfigs — configure which tools each agent profile has access to

Last-used agent persistence — remembers which agent you used last

Improvements

Session Resume fixed — no need to re-explain context to the agent

Montage Editing improved

v0.2.0 — February 7, 2026

Our biggest update yet — 3 new tools, full UI overhaul, and a bunch of crash fixes.

New Tools

Animation Montage Editing — Add/remove/link sections, add notifies (typed, state, branching points), configure blend settings

Enhanced Input Editing — Create & edit InputActions and InputMappingContexts, all trigger & modifier types supported

Viewport Screenshot — Agent can now see what's in your viewport. Captures the active viewport as an image with camera info

Major Upgrades

Level Sequencer completely rewritten — dynamic discovery instead of hardcoded tracks. Supports any track type in the engine now, bulk transforms in one call

Behavior Trees — Agent can now set any node property directly (AcceptableRadius, WaitTime, FlowAbortMode, etc.)

Asset Reading — BT reads now always show full node tree + blackboard details. Blueprint vars show replication flags. New readers for Enhanced Input & Gameplay Effects. Level Sequence reads are way more detailed now

UI Overhaul

Thinking content now renders full markdown (bold, italic, code, clickable file paths)

New "Think" toggle for Claude Code — control how much the agent reasons

Streaming no longer bounces — smooth throttled updates

Animated tab switching, hover states, animated history sidebar

Message layout redesigned — cleaner look, copy on hover, rounded user messages

Session saving no longer freezes UI on tab switch

Crash Fixes

Fixed crash when agents send full asset paths as variable subtypes

Fixed AnimGraph node title crash on read

Fixed blueprint compile crash

PIE log reading improved

Security

MCP server now blocks browser requests by default (CSRF protection). Can be enabled in settings if needed.

v0.1.36

Animation Blueprint support made much better, crashes should be very minimal now

Fixed bugs relating to Session Resume for Claude Code

You can now open Chat using the bottom bar

Agent can now read 3 more file types, including Chooser Tables

Behaviour Tree issues fixed

v0.1.34

Gemini, Cursor, Copilot, Local LLM support added/made better

Fixed 45 crash issues — major stability update

v0.1.33

Gameplay Tags support added

Animation Blueprint crash fixes (state machines, transitions no longer crash the editor)

Undo/Redo now works properly across all tools

IK Rig & Pose Search stability improvements

Behavior Tree node initialisation fixed

v0.1.28

Chooser Table support added

Cross Blueprint Referencing now supported (AI can grab references/functions from inside components and blueprints)

Functions fully re-written for better support

MultiCast Custom Events fixed

Added support for changing text size

Fixed Codex and OpenCode support

v0.1.25

State Trees support added

Live Coding support added — agent can do live compile and fix issues

UI reworked — copy text, table and code renders fixed

Mode selection for agents is now persisted

v0.1.23

Deep component settings and configurations

Can now read, screenshot and view almost any asset

Speed improvements

Fixes for Mac (Node.js detection issues)

Fixes for PCG and Control Rig

v0.1.22

Motion Matching support added

MetaSounds support added

Huge refactor of Read tool — agent can now read almost all major UE file types

v0.1.21

IK Retargeter support added — retarget animations between different skeletons through natural language

PCG issues fixed

Claude Code session leaking fixed

OpenRouter made more stable

v0.1.18

Procedural Content Generation Framework (PCG) support added

Cursor Agent CLI added

Automatic ACP adaptor installation

Other languages support improved

v0.1.14

IK Rig support added — create solvers, goals, retarget chains

Auto-generate FBIK and retarget setups for humanoids

v0.1.13

OpenCode support added

Cost Metrics shown for OpenRouter

Initial support for Niagara VFX and Level Sequencer

Custom Event Creation and Variables Replication fixed

Timeline support added

v0.1.11

Parallel Chats & Conversation History added

Plan Mode added

Polished UI

Todo list support for Claude Code

GitHub Copilot support added

Enable/Disable tools in settings

v0.1.10

Image Generation + 3D Model Generation using Meshy

Agent can now screenshot models and listen to audio

Initial support for Niagara VFX

Initial support for Project Memory

Support for 5.6, 5.5 and Mac

Live cost display when Agent is running

Comparison

See how Agent Integration Kit compares to every other AI tool for Unreal Engine.

On this page

v0.5.6 — February 21, 2026 What's Better Other Improvements v0.5.5 — February 18, 2026 What’s Better Stability Improvements v0.5.0 — February 16, 2026 Completely New UI Agents Are Smarter with Fewer Tools Agent Can See What You See Zero-Setup Install Continue Conversations Across Agents Environment Query System (EQS) Support Much More Stable Other Improvements v0.4.0 — February 10, 2026 Animation Tools — 100% Coverage Improvements v0.3.2 — February 9, 2026 v0.3.0 — February 8, 2026 Blueprint Editing — 100% Coverage Stability New Features Improvements v0.2.0 — February 7, 2026 New Tools Major Upgrades UI Overhaul Crash Fixes Security v0.1.36 v0.1.34 v0.1.33 v0.1.28 v0.1.25 v0.1.23 v0.1.22 v0.1.21 v0.1.18 v0.1.14 v0.1.13 v0.1.11 v0.1.10
