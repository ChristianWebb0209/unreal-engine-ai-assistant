workspace "Unreal AI Editor Plugin Architecture" "Detailed C4 architecture with subsystem decomposition for UnrealAiEditor" {
    !identifiers hierarchical

    model {
        dev = person "Unreal Developer" "Uses the in-editor assistant to plan, inspect, edit, and automate Unreal workflows."

        unrealEditor = softwareSystem "Unreal Editor Runtime" "Engine/editor surfaces: selection, Asset Registry, Content Browser, open editor tabs, world state, PIE, and command interfaces." "External"
        llmProvider = softwareSystem "Third-Party LLM APIs (BYOK)" "User-configured model endpoints (OpenRouter/OpenAI/Anthropic-compatible)." "External"
        localhostBridge = softwareSystem "Localhost Integrations (Optional)" "Optional MCP/CLI local bridge endpoints and local child-process integrations." "External"

        plugin = softwareSystem "UnrealAiEditor Plugin" "Single in-editor, local-first runtime with no required product backend." {
            ui = container "UI Layer (Slate)" "Chat/settings/debug tabs and composer UX." "C++ / Slate" "UI" {
                chatTab = component "Chat Tab Shell" "Dockable chat tab and high-level UI state." "SUnrealAiEditorChatTab" "UI"
                composer = component "Composer + Send Pipeline" "Send handling, typed input, mode state, and run kick-off." "SChatComposer" "UI"
                settingsTab = component "Settings Surfaces" "Provider/model/profile/retrieval/memory settings." "SUnrealAiEditorSettingsTab" "UI"
                chatTranscript = component "Transcript Widgets" "Message rendering, tool call panels, user-visible warnings." "Slate Widgets" "UI"
                quickStart = component "Quick Start + Debug Tabs" "Onboarding and diagnostics controls." "Slate Widgets" "UI"
            }

            backendRegistry = container "Backend Registry" "Composition root that constructs and wires core services." "C++" "Core"
            policy = container "Policy + Guard Rails" "Mode gating, permission checks, and deterministic policy constants." "C++" "Policy"

            modelProfiles = container "Model Profile Registry" "Provider/model profile resolution and effective capability computation." "C++" "Core" {
                settingsLoader = component "Settings Loader" "Loads providers/models/defaults from settings JSON." "FUnrealAiModelProfileRegistry" "Core"
                capResolver = component "Capability Resolver" "Resolves effective caps for requested model profile." "FUnrealAiModelProfileRegistry" "Core"
                apiResolver = component "API Endpoint Resolver" "Resolves base URL/API key by model/provider." "FUnrealAiModelProfileRegistry" "Core"
                curatedModelCatalog = component "Curated Model Catalog" "Built-in provider -> known OpenAI-compatible model id list used by settings pickers." "SUnrealAiEditorSettingsTab" "Core"
            }

            promptLibrary = container "Prompt Chunk Library" "Prompt chunk templates and token replacement contracts." "Markdown + C++" "Core"

            requestBuilder = container "Turn Request Builder" "Builds request payloads from context, conversation, profile caps, and prompt chunks." "C++" "Core" {
                promptAssembler = component "Prompt Assembler" "Combines chunks/tokens and context block placeholders." "UnrealAiPromptBuilder" "Core"
                messageBudgeter = component "Message Budgeter" "Character/token approximation and message trimming." "UnrealAiTurnLlmRequestBuilder" "Policy"
                capabilityRouter = component "Capability Router" "Model image/tool constraints and include flags." "UnrealAiTurnLlmRequestBuilder" "Policy"
            }

            harness = container "Agent Harness" "Turn loop orchestration, tool rounds, continuation logic, and cancellation handling." "C++" "Core" {
                turnOrchestrator = component "Turn Orchestrator" "Controls request rounds and stop conditions." "FUnrealAiAgentHarness" "Core"
                toolLoop = component "Tool Loop Coordinator" "Captures tool call results and continuation transitions." "FUnrealAiAgentHarness" "Core"
                continuationRails = component "Continuation Rails" "Round/time/cancel guard rails and mode behavior." "FUnrealAiAgentHarness" "Policy"
                workerOrchestration = component "Worker/Subagent Coordination" "Parent-worker flows and merge points (Agent mode)." "FUnrealAiAgentHarness" "Core"
                runSink = component "Run Artifact Sink" "Writes run artifacts and step status metadata." "FAgentRunFileSink" "Core"
            }

            transport = container "LLM Transport" "Provider transport abstraction and concrete OpenAI-compatible streaming parser." "C++" "Core" {
                httpAdapter = component "OpenAI-Compatible HTTP Adapter" "Sends chat-completions style requests and handles SSE/non-SSE." "FOpenAiCompatibleHttpTransport" "Core"
                streamParser = component "Stream Event Parser" "Normalizes deltas, tool calls, finish reasons, usage." "FOpenAiCompatibleHttpTransport" "Core"
            }

            tools = container "Tooling Runtime" "Tool catalog, dispatch, and execution host for editor-facing operations." "C++" "Core" {
                catalogLoader = component "Tool Catalog Loader" "Loads/parses UnrealAiToolCatalog.json metadata." "UnrealAiToolCatalog" "Core"
                executionHost = component "Tool Execution Host" "Permission checks, dispatch invocation, result envelope shaping." "FUnrealAiToolExecutionHost" "Core"
                dispatchActors = component "Actors + World Dispatch" "Actor/transform/world operations." "UnrealAiToolDispatch_Actors" "Core"
                dispatchAssets = component "Assets + Content Dispatch" "Asset CRUD, browser sync/navigation, metadata." "UnrealAiToolDispatch_*Assets*" "Core"
                dispatchBlueprint = component "Blueprint Dispatch" "Blueprint summary/export/edit helpers and graph operations." "UnrealAiToolDispatch_BlueprintTools" "Core"
                dispatchEditorUi = component "Editor UI Dispatch" "Tab/menu/mode and focus commands." "UnrealAiToolDispatch_EditorUi" "Core"
                dispatchSearch = component "Search + File Dispatch" "Project text/symbol search and file read/write wrappers." "UnrealAiToolDispatch_Search" "Core"
                dispatchPie = component "PIE Dispatch" "Play-in-editor controls and status operations." "UnrealAiToolDispatch_Pie" "Core"
                assetResolver = component "Asset Factory Resolver" "Type-specific asset creation/resolution behavior." "UnrealAiAssetFactoryResolver" "Core"
            }

            contextService = container "Context Service" "Thread-scoped context state and ranked context assembly pipeline." "C++" "ContextSection" {
                sessionStore = component "Thread Context Session Store" "Load/create/in-memory map of thread state objects." "FUnrealAiContextService" "ContextSection"
                editorSnapshot = component "Editor Snapshot Query Adapter" "Reads selected actors/assets, open editors, folders." "UnrealAiEditorContextQueries" "ContextSection"
                mentionParser = component "@ Mention Resolver" "Resolves @tokens to canonical asset references." "UnrealAiContextMentionParser" "ContextSection"
                candidateCollector = component "Candidate Collector" "Collects envelope candidates by source type." "UnrealAiContextCandidates" "ContextSection"
                candidateScorer = component "Ranking + Scoring Engine" "Applies weighted relevance/recency/mention/safety scoring." "UnrealAiContextCandidates + RankingPolicy" "ContextSection"
                packer = component "Budgeted Context Packer" "Per-type caps and budget-aware keep/drop packing." "UnrealAiContextCandidates" "ContextSection"
                complexityAssessor = component "Complexity Assessor" "Deterministic complexity signal generation and plan recommendation." "FUnrealAiComplexityAssessor" "ContextSection"
                contextFormatter = component "Context Formatter" "Builds machine-readable context blocks and summary text." "AgentContextFormat" "ContextSection"
                contextJson = component "Context JSON Serializer" "Schema versioning, load/save, migration compatibility." "AgentContextJson" "ContextSection"
            }

            memoryService = container "Memory Service" "Memory query/extract/compact/prune lifecycle." "C++" "MemorySection" {
                queryEngine = component "Memory Query Engine" "Staged title/description/body retrieval contract." "FUnrealAiMemoryService" "MemorySection"
                compactor = component "Memory Compactor" "Extraction/compaction heuristic pass." "FUnrealAiMemoryCompactor" "MemorySection"
                retention = component "Retention + Tombstones" "Pruning, deletion ledger, anti-regeneration controls." "FUnrealAiMemoryService" "MemorySection"
                memoryJson = component "Memory JSON Persistence" "Reads/writes memory index/item files." "Memory serializers" "MemorySection"
            }

            retrievalService = container "Retrieval Service (Optional)" "Local vector indexing/query and compatibility/migration handling." "C++" "RetrievalSection" {
                indexManager = component "Index Lifecycle Manager" "Init/integrity/rebuild and manifest transitions." "FUnrealAiRetrievalService + VectorIndexStore" "RetrievalSection"
                queryEngine = component "Retrieval Query Engine" "Embed query + top-K cosine + result shaping." "FUnrealAiRetrievalService" "RetrievalSection"
                chunkPipeline = component "Chunking + Source Inventory" "File/docs/blueprint/memory chunk generation pipeline." "FUnrealAiRetrievalService" "RetrievalSection"
                migrationGuard = component "Model Compatibility Guard" "Handles mismatch, pending_reembed, fail-closed behavior." "FUnrealAiRetrievalService" "Policy"
                store = component "Vector Index Store Adapter" "SQLite storage and manifest I/O abstraction." "UnrealAiVectorIndexStore" "RetrievalSection"
            }

            embeddingProvider = container "Embedding Provider (Optional)" "Embedding adapter interface + current OpenAI-compatible implementation." "C++" "RetrievalSection" {
                openAiCompat = component "OpenAI-Compatible Embedding Adapter" "Calls /embeddings and decodes vector arrays." "FOpenAiCompatibleEmbeddingProvider" "RetrievalSection"
            }

            observability = container "Observability + Diagnostics" "Decision logs, audits, workflow status, diagnostics exports, and usage metrics." "C++" "Core" {
                contextDecisionLogs = component "Context Decision Logger" "Per-turn keep/drop events and summary markdown." "UnrealAiContextDecisionLogger" "Core"
                toolAuditLogger = component "Tool Audit Logger" "Structured tool execution audit records." "Tool audit paths" "Core"
                retrievalDiag = component "Retrieval Diagnostics" "Index status and diagnostics export rows." "UnrealAiRetrievalDiagnostics" "Core"
                usageTracker = component "Usage Tracker" "Token/cost metric aggregation." "FUnrealAiUsageTracker" "Core"
                pricingCatalog = component "Curated Pricing Catalog" "Small in-code rough pricing map used for usage estimates." "FUnrealAiModelPricingCatalog" "Core"
                workflowStatus = component "Workflow Status Writer" "Reliability artifacts and step/workflow status files." "workflow status emitters" "Core"
            }

            dataRoot = container "Local Data Root" "%LOCALAPPDATA%/UnrealAiEditor namespace owner." "Filesystem" "Data"
        }

        localData = softwareSystem "Local Data Store" "On-disk data owned by plugin runtime." {
            settingsJson = container "settings/plugin_settings.json" "Providers, model profiles, defaults, feature toggles, caps." "JSON file" "Data"
            usageStatsJson = container "settings/usage_stats.json" "Aggregated usage and pricing-adjacent counters." "JSON file" "Data"
            conversationJson = container "chats/<project>/threads/<thread>/conversation.json" "Role-ordered conversation transcript." "JSON file" "Data"
            contextJson = container "chats/<project>/threads/<thread>/context.json" "Context state, attachments, snapshot, plans, DAG status." "JSON file" "Data"
            memoriesIndex = container "memories/index.json" "Memory catalog metadata index." "JSON file" "Data"
            memoriesItems = container "memories/items/*.json" "Full memory payloads." "JSON files" "Data"
            memoriesTombstones = container "memories/tombstones.json" "Deletion history for regeneration suppression." "JSON file" "Data"
            vectorDb = container "vector/<project>/index.db" "Optional local vector chunk + embedding store." "SQLite" "Optional"
            vectorManifest = container "vector/<project>/manifest.json" "Index/migration/status metadata." "JSON file" "Optional"
            diagnostics = container "Saved/UnrealAiEditor/*" "Tool audit, context decisions, workflow status artifacts." "Logs/JSONL/Markdown" "Data"
        }

        dev -> plugin "Uses in editor"
        plugin.ui -> plugin.backendRegistry "Bootstraps dependencies"
        plugin.ui -> plugin.contextService "Load context + snapshot + mention state"
        plugin.ui -> plugin.harness "RunTurn requests"
        plugin.ui -> plugin.modelProfiles "Profile/provider/model selection"
        plugin.ui -> plugin.policy "Mode + confirmation gating"

        plugin.backendRegistry -> plugin.modelProfiles "Constructs"
        plugin.backendRegistry -> plugin.requestBuilder "Constructs"
        plugin.backendRegistry -> plugin.harness "Constructs"
        plugin.backendRegistry -> plugin.transport "Constructs"
        plugin.backendRegistry -> plugin.tools "Constructs"
        plugin.backendRegistry -> plugin.contextService "Constructs"
        plugin.backendRegistry -> plugin.memoryService "Constructs"
        plugin.backendRegistry -> plugin.retrievalService "Constructs optional"
        plugin.backendRegistry -> plugin.embeddingProvider "Constructs optional"
        plugin.backendRegistry -> plugin.observability "Constructs"
        plugin.backendRegistry -> plugin.modelProfiles.curatedModelCatalog "Settings pick-list source"

        plugin.harness -> plugin.requestBuilder "Build request each round"
        plugin.harness -> plugin.transport "Send model call"
        plugin.harness -> plugin.tools "Execute tool calls"
        plugin.harness -> plugin.contextService "Record bounded tool results"
        plugin.harness -> plugin.observability "Emit run diagnostics"

        plugin.requestBuilder -> plugin.contextService "BuildContextWindow"
        plugin.requestBuilder -> plugin.promptLibrary "Read chunks/templates"
        plugin.requestBuilder -> plugin.modelProfiles "Resolve caps/provider"
        plugin.requestBuilder -> plugin.policy "Apply budget + capability policy"

        plugin.tools -> unrealEditor "Calls editor APIs"
        plugin.tools -> plugin.policy "Permission + confirmation checks"
        plugin.tools -> plugin.contextService "Send tool result snippets"
        plugin.tools -> plugin.observability "Audit and diagnostics"

        plugin.contextService -> plugin.memoryService "Query memory snippets"
        plugin.contextService -> plugin.retrievalService "Query retrieval snippets (optional)"
        plugin.contextService -> plugin.policy "Apply mode gates and type filters"
        plugin.contextService -> plugin.observability "Keep/drop decision logs"

        plugin.retrievalService -> plugin.embeddingProvider "Embed query/chunks"
        plugin.retrievalService -> plugin.policy "Compatibility/fail-closed checks"
        plugin.retrievalService -> plugin.observability "Index diagnostics"
        plugin.embeddingProvider -> plugin.modelProfiles "Resolve API endpoint/key"

        plugin.transport -> llmProvider "HTTPS chat requests/streams"
        plugin.embeddingProvider -> llmProvider "HTTPS embedding requests"
        plugin.ui -> localhostBridge "Optional local bridge controls"

        plugin.dataRoot -> localData.settingsJson "Owns"
        plugin.dataRoot -> localData.usageStatsJson "Owns"
        plugin.dataRoot -> localData.conversationJson "Owns"
        plugin.dataRoot -> localData.contextJson "Owns"
        plugin.dataRoot -> localData.memoriesIndex "Owns"
        plugin.dataRoot -> localData.memoriesItems "Owns"
        plugin.dataRoot -> localData.memoriesTombstones "Owns"
        plugin.dataRoot -> localData.vectorDb "Owns optional"
        plugin.dataRoot -> localData.vectorManifest "Owns optional"
        plugin.dataRoot -> localData.diagnostics "Owns"

        plugin.modelProfiles -> localData.settingsJson "Load settings"
        plugin.observability.usageTracker -> plugin.observability.pricingCatalog "Estimate rough USD totals"
        plugin.requestBuilder -> localData.conversationJson "Read/write transcript"
        plugin.contextService -> localData.contextJson "Read/write context"
        plugin.memoryService -> localData.memoriesIndex "Read/write memory index"
        plugin.memoryService -> localData.memoriesItems "Read/write memory items"
        plugin.memoryService -> localData.memoriesTombstones "Read/write tombstones"
        plugin.retrievalService -> localData.vectorDb "Read/write vectors"
        plugin.retrievalService -> localData.vectorManifest "Read/write manifest"
        plugin.observability -> localData.diagnostics "Write diagnostics"
        plugin.harness -> localData.usageStatsJson "Update usage counters"
    }

    views {
        systemContext plugin "system-context" "System context: user + plugin + external boundaries." {
            include *
            autoLayout lr
        }

        container plugin "plugin-containers" "High-level plugin container architecture with persistence and externals." {
            include *
            autoLayout lr
        }

        component plugin.contextService "context-components" "Context subsystem decomposition." {
            include *
            autoLayout lr
        }

        component plugin.harness "harness-components" "Harness and orchestration decomposition." {
            include *
            autoLayout lr
        }

        component plugin.tools "tooling-components" "Tool host and dispatch decomposition." {
            include *
            autoLayout lr
        }

        component plugin.retrievalService "retrieval-components" "Optional retrieval subsystem decomposition." {
            include *
            autoLayout lr
        }

        component plugin.memoryService "memory-components" "Memory subsystem decomposition." {
            include *
            autoLayout lr
        }

        component plugin.ui "ui-components" "UI subsystem decomposition." {
            include *
            autoLayout lr
        }

        dynamic plugin "request-lifecycle" "Turn lifecycle with core stages and persistence writes." {
            plugin.ui -> plugin.contextService "1. Load/create thread session + mentions + snapshot refresh"
            plugin.ui -> plugin.harness "2. Start RunTurn"
            plugin.harness -> plugin.requestBuilder "3. Assemble request and prompts"
            plugin.requestBuilder -> plugin.contextService "4. Build ranked context window"
            plugin.requestBuilder -> plugin.modelProfiles "5. Resolve model capabilities/provider route"
            plugin.harness -> plugin.transport "6. Send model request"
            plugin.transport -> llmProvider "7. Stream response"
            plugin.harness -> plugin.tools "8. Execute tool calls (if emitted)"
            plugin.tools -> unrealEditor "9. Perform editor operations"
            plugin.tools -> plugin.contextService "10. Record tool result snippets"
            plugin.harness -> plugin.observability "11. Emit run artifacts + diagnostics"
            plugin.requestBuilder -> localData.conversationJson "12. Persist conversation updates"
            plugin.contextService -> localData.contextJson "13. Persist context updates"
            autoLayout lr
        }

        styles {
            element "Person" {
                shape person
                background #E8F1FF
                color #0F172A
                stroke #3B82F6
            }
            element "Software System" {
                background #EAFBF1
                color #052E1A
                stroke #10B981
            }
            element "Container" {
                background #EAFBF1
                color #052E1A
                stroke #10B981
            }
            element "Component" {
                background #FFFFFF
                color #111827
                stroke #9CA3AF
            }
            element "External" {
                background #F3F4F6
                color #111827
                stroke #6B7280
            }
            element "UI" {
                background #E8F1FF
                color #0F172A
                stroke #3B82F6
            }
            element "ContextSection" {
                background #E0F2FE
                color #0C4A6E
                stroke #0284C7
            }
            element "MemorySection" {
                background #ECFDF5
                color #065F46
                stroke #10B981
            }
            element "RetrievalSection" {
                background #F5F3FF
                color #4C1D95
                stroke #8B5CF6
            }
            element "Policy" {
                background #FFEAEA
                color #7F1D1D
                stroke #EF4444
            }
            element "Data" {
                shape cylinder
                background #FFF7E6
                color #3B2A00
                stroke #F59E0B
            }
            element "Optional" {
                background #F5F3FF
                color #4C1D95
                stroke #8B5CF6
            }
        }

        theme default
    }
}
