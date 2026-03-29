# Todo List

## Repo Organization

- We need to completely refactor the testing and scripts folder. There's so much garbage there.
- - Probably: /tests/long-running-tests just becomes /tests/ and we remove all of the old harness stuff.
- - Delete all of the old runs that don't fit the newest schema. Do we have all of the insight we need out of old runs? 
- - - Maybe we make a brief summary of the progression by having an agent go through and note a ton of things from all the runs before we delete them.

## Blueprint Organizer (In separate plugin)

- Verify everything is working
- - Verify - that there are two buttons added at the top of the blueprint editor event graph: organize all, and (only if user has selected something), organize selected. Both buttons need to have icons and tooltip hints.

## Tooling

- Ensure tools are working for PERFORMANT and FAST: multi-select, select by class, fuzzy search for selection (within scene or within file tree).
- - Ensure fast workflow for batch operations like replace material on every wall in scene, etc. We should make a testing situation for this.
- Ensure tools catalog has high coverage of editor functionality:
- - Data assets, animation basics, building components within components (building an actor then attatching a collision mesh, etc.)

# Future Features

- "Asset Fetcher" setting. When toggled, it will enable a tool (which corresponds with a service) that will interface with FAB or some other asset library to allow LLM to get free fairuse assets, import them into project, and wire them up.
- - This will be super hard because FAB doesn't expose a lot of useful APIs for us to use. We may have to do a hybrid approach, where our assistant will open a FAB instance and listen for what the user selects, then try to make use of whatever the user imports.
- Stage changes before making them. Add checkpoints and reverting capabilities.
- Unreal docs RAG
- Unreal forums RAG
- Support for other versions, ideally all 5.x verisions. Currently this only works for 5.7.
