
### Refactor idea

I'm thinking we should refactor / rebuild our tooling, prompting, and resolving system in the following ways:

- The prompt chunks folder will have subfolders within it, one being for blueprint creation (in general we should refactor the prompt chunks into a few subfolders based on certain modes. Most of the chunks will still be in root dir, but that's ok)
- - Blueprint creation essentially works like a separate query mode, but it isn't user selectable. Only agent queries can return it. Agent queries should get a prompt chunk that specifies fairly quickly that it can return build-blueprint
- - This will have in it detailed instructions for a query that is just building either one specific blueprint or one group of blueprints
- - Including instructions on best code practices, building new blueprints when needed, refactoring logic into separate blueprints, formatting
- We will change our main prompt / query system so that we never expose tools to build blueprints
- Instead, in the main query, we have a separate prompt chunk for returning blueprints, which will tell the agent how to return a custom build-blueprint tag, with a full new query inside that has all of the specificaitons of the blueprint / batch of blueprints it should build

### Blueprint builder mode

Following our other documentation, the main goal of this mode is to give tools and prompts that make building blueprints as deterministic as possible. 

- This means we should expose a very large number of tools, with implementation being pushed onto the tool resolvers, and little room for agent error. 
- Since we will build a separate query altogether for building a blueprint / batch of blueprints, we can be much more specific with what we add to context.
- We currently have a tool picker that picks the top k most relevant tools to send to context in full, but also sends the names of most of the other tools so the agent can return back asking for those ones in more detail if needed.
- - We should expand this to have a mode (or maybe a separate related component) that picks tools for the blueprint creator only
- - This means the build-blueprint agent will get a full set of custom tools that aren't available to the main agent (as well as probably some common things like making files)
- When this build-blueprint tag gets resolved, it will automatically queue another query with all of the instructions in the blueprint prompt chunks, as well as custom tools selected for actually building and refactoring the blueprints.
- One of the chunks is instructions on how to fail safely from blueprint create, including if there is other functionality outside the blueprint system that it realizes it must implement
- - It should have a failure mode where it can send instructions back to the original agent (which include information about whether or not it finished), in which case, the original agent will get queried again (with the same context as before) and a new prompt chunk specifying that it had asked for x blueprint to be made, and the blueprint creation tool responded with y

### Recap

- Ask
- Agent
- - Can emit plans which automatically call new agent queries
- - Each agent query can also emit blueprint queries
- Plan
- - Builds a plan which the user has to approve that is made up of many agent queries

### Lessons from competitors

Taken from arch-analysis.md:

- Autonomix is one of the clearest “big bang” Blueprint creation competitors:
- - It avoids the hard problem of “LLM must invent Unreal internal GUIDs” by using a GUID placeholder system (symbolic tokens that are resolved deterministically during import).
- - It uses T3D (Unreal clipboard text format) as its authoring medium, which lets the model generate a whole graph in one payload instead of calling node APIs one-by-one.
- Creation loop (as described):
- - (For existing BPs) call get_blueprint_info first (pin audit + T3D readback).
- - Generate an entire node graph in T3D with placeholder GUIDs.
- - Perform pre-flight validation (reflection-based checks) before applying.
- - Import nodes with FEdGraphUtilities::ImportNodesFromText (single atomic operation).
- - Run compile + a structured verification ladder (compile_blueprint, then verify_blueprint_connections, etc.).
- Competitive strength: it treats “Blueprint graph generation” as a first-class atomic payload problem, with validation and retry handled at the tool level.

### Open questions

- Should we allow the main agent to emit a blueprint-builder with their choice of: interject task then continue with response in mind (this would work like stopping then starting the query again where we left off) - or - queue task on end
- Should the main agent still expose tools to make blueprints and attatch them? Probably yes, but if so, should the blueprint builder also have these tools? Maybe we should choose one or the other to separate concerns fully. Should the main agent delegate paths to the blueprint builder, or should the responsability be on both agents to find adequate paths?
The main agent will handle everything and we will specifically prompt it (in the description of how to emit a blueprint builder response) that it must create full valid paths with blueprints first and send a path to blueprint builder for any blueprint it wants to edit. This means the main agent will have to think about attatching blueprints to actors, etc. The blueprint builder will have a prompt chunk telling it to optionally return that the architecture is poor, and suggest (in plain language) a way to change it, then return with status, what it has done, and what remains to be done.