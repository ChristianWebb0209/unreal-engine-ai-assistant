# Todo List:

## UI

- [ ] Text glitch where it bunches up and doesn't animate dropping down correctly
- [x] Subagents should have a more rich indication to the user that they are working
  - Plan-node worker lanes (`SPlanWorkerLanePanel`): plan accent bar, edit icon, **Working…** + **SCircularThrobber** while running; status colors; optional summary line when the node finishes.
  - [ ] Maybe something graph based and each concurrent agent is shown side by side next to each other *(still open: executor runs one plan node at a time; parallel side-by-side needs true concurrent waves + layout pass)*
- [x] We need to brainstorm a way to make context window stats visible and readable to user *(initial implementation for plan workers)*
  - [x] For subagents, maybe depending on how we choose to display each subagent, there will be a circle like in Cursor to show next to each agent how full its window is *(replaced with a **linear progress bar** + fraction label: `Context (est.): ~prompt / max tok`; ring-style control can be added later)*
  - [x] Hovering over any of these should show tooltip that is the actual tokens / max tokens (max tokens is defined in user settings) *(tooltip on the context row: estimated prompt tokens vs model profile **MaxContextTokens**)*
- [x] We should have the chats just be named by the first user message, and not do anything complicated with custom prompts to decide a name
  - [x] It named my chat after the second message I sent for some reason?
  - [x] It named my chat the first line of the agent response this time for some reason?
- [x] "---" and "[Harness]" are often still not resolved in chat renderer
- [x] We need some richer icons and colors to show in the chat renderer *(plan worker lanes: `Icons.Edit` + plan accent; status chip coloring; context bar uses plan accent fill)*
- [ ] TONS of deprecated code everywhere. Pretty much the whole project settings / plugin tab, and most of our plugin settings tab are now deprecated. Including everything to do with open-router
- [ ] Animations for chat renderer are terribly broken. Probably just remove all of this and make it simpler

## Bugs

- blueprint_graph_patch still fails sometimes
- Agent occasionally switches to front view in viewport for no discernable reason
- Performance is still terrible, with the whole editor freezing when performing simple tasks

## Features

- Editor follow shouldn't be able to be enabled on two running agents at the same time
- Editor follow should be off by default since it causes a lot of lag
- (maybe) We should make agent types for each of the blueprint types, then one is just for general blueprint stuff, instead of having one type that does all different types of blueprint



- We need to fully build out the pcg and scene builder, much of it is stubbed right now
- Future: make an assistant for grabbing or scraping free / no copyright assets online into project
- Performance: see if its possible to improve freezing anymroe. I think current unreal engines are notorious for being bad with this, and they may fix it in the near future.
- Add support for other 5.x versions, right now we are just 5.7