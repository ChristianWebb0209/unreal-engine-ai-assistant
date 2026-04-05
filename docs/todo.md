# Todo List:

## UI

- [ ] Text glitch where it bunches up and doesn't animate dropping down correctly
- [ ] Subagents should have a more rich indication to the user that they are working
  - [ ] Maybe something graph based and each concurrent agent is shown side by side next to each other
- [ ] We need to brainstorm a way to make context window stats visible and readable to user
  - [ ] For subagents, maybe depending on how we choose to display each subagent, there will be a circle like in Cursor to show next to each agent how full its window is
  - [ ] Hovering over any of these should show tooltip that is the actual tokens / max tokens (max tokens is defined in user settings)
- [x] We should have the chats just be named by the first user message, and not do anything complicated with custom prompts to decide a name
  - [x] It named my chat after the second message I sent for some reason?
  - [x] It named my chat the first line of the agent response this time for some reason?
- [x] "---" and "[Harness]" are often still not resolved in chat renderer
- [ ] We need some richer icons and colors to show in the chat renderer
- [ ] TONS of deprecated code everywhere. Pretty much the whole project settings / plugin tab, and most of our plugin settings tab are now deprecated. Including everything to do with open-router
- [ ] Animations for chat renderer are terribly broken. Probably just remove all of this and make it simpler

## Bugs

- blueprint_graph_patch still fails tremendously for some reason
- Agent occasionally switches to front view in viewport for no discernable reason
- Performance is still terrible, with the whole editor freezing when performing simple tasks

## Features

- Editor follow shouldn't be able to be enabled on two running agents at the same time
- Editor follow should be off by default since it causes a lot of lag
- (maybe) We should make agent types for each of the blueprint types, then one is just for general blueprint stuff, instead of having one type that does all different types of blueprint
- We need to make a demo for the project preferrably using OpenScreen
