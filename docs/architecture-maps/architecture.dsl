// C4 workspace for architecture maps (exported to SVG via scripts/export-architecture-maps.ps1).
// Diagram readability: very long single-line descriptions render as one stretched horizontal line in SVGs
// (no automatic wrap). Prefer splitting description strings with \n at clause boundaries or ~60-80 characters.
// Keep relationship labels short; put narrative detail in the readme prose block (see file tail) or docs/.
workspace "Unreal AI Editor Plugin Architecture" "Detailed C4 architecture with subsystem decomposition for UnrealAiEditor" {
    !identifiers hierarchical

    model {
        dev = person "Unreal Developer" "Uses the in-editor assistant to plan, inspect, edit,\nand automate Unreal workflows."

        unrealEditor = softwareSystem "Unreal Editor Runtime" "Engine/editor surfaces: selection, Asset Registry, Content Browser,\nopen editor tabs, world state, PIE, and command interfaces." "External"
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

            requestBuilder = container "Turn Request Builder" "Builds request payloads from context, conversation, profile caps, prompt chunks,\nand tool wire format (native tools[] vs unreal_ai_dispatch + markdown index)." "C++" "Core" {
                promptAssembler = component "Prompt Assembler" "Combines chunks/tokens and context block placeholders." "UnrealAiPromptBuilder" "Core"
                messageBudgeter = component "Message Budgeter + Tool Wire" "UnrealAiTurnLlmRequestBuilder: conversation + system text,\noptional tiered tool surface (TryBuildTieredToolSurface), message trimming." "UnrealAiTurnLlmRequestBuilder" "Policy"
                capabilityRouter = component "Capability Router" "Model image/tool constraints and include flags." "UnrealAiTurnLlmRequestBuilder" "Policy"
            }

            harness = container "Agent Harness" "Turn loop orchestration, tool rounds, continuation logic, cancellation handling,\ntool surface telemetry, usage logging, optional repair nudge after bad unreal_ai_dispatch unwrap." "C++" "Core" {
                turnOrchestrator = component "Turn Orchestrator" "Controls request rounds and stop conditions." "FUnrealAiAgentHarness" "Core"
                toolLoop = component "Tool Loop Coordinator" "Streams tool calls; executes via host.\ntool_surface_metrics enforcement events; JSONL usage + session prior;\nbounded repair user line." "FUnrealAiAgentHarness" "Core"
                continuationRails = component "Continuation Rails" "Round/time/cancel guard rails and mode behavior." "FUnrealAiAgentHarness" "Policy"
                workerOrchestration = component "Worker/Subagent Coordination" "Parent-worker flows and merge points (Agent mode)." "FUnrealAiAgentHarness" "Core"
                runSink = component "Run Artifact Sink" "Writes run artifacts and step status metadata." "FAgentRunFileSink" "Core"
            }

            transport = container "LLM Transport" "Provider transport abstraction and concrete OpenAI-compatible streaming parser." "C++" "Core" {
                httpAdapter = component "OpenAI-Compatible HTTP Adapter" "Sends chat-completions style requests and handles SSE/non-SSE." "FOpenAiCompatibleHttpTransport" "Core"
                streamParser = component "Stream Event Parser" "Normalizes deltas, tool calls, finish reasons, usage." "FOpenAiCompatibleHttpTransport" "Core"
            }

            tools = container "Tooling Runtime" "Tool catalog, dispatch, execution host, optional tool surface pipeline\n(lexical eligibility + tiered markdown for unreal_ai_dispatch)." "C++" "Core" {
                catalogLoader = component "Tool Catalog Loader" "Loads/parses UnrealAiToolCatalog.json; builds native tools[] or dispatch index.\nTiered appendix: roster lines + expanded parameter excerpts under budget\n(BuildCompactToolIndexAppendixTiered)." "FUnrealAiToolCatalog" "Core"
                toolSurfacePipeline = component "Tool Surface Pipeline" "Default on (toggle ToolEligibilityTelemetryEnabled in UnrealAiRuntimeDefaults.h).\nAgent + dispatch round 1: shaping, BM25, bias, session prior blend,\ndynamic K, guardrails, tiered markdown.\nSeparate from docs vector retrieval." "UnrealAiToolSurfacePipeline" "Core"
                toolQueryShaper = component "Tool Query Shaper" "Cheap heuristic intent line (verbs/objects) + hybrid query string.\nPrevents a bad extractor from dominating raw user text." "UnrealAiToolQueryShaper" "Core"
                toolLexicalIndex = component "Tool Lexical Index (BM25)" "In-memory BM25 over enabled tool text (id, summary, category, tags).\nKeyword leg for hybrid retrievalâ€”not embeddings." "FUnrealAiToolBm25Index" "Core"
                toolDynamicKPolicy = component "Dynamic K Policy" "Chooses effective K from score margin between top candidates\n(min/max env caps), then caps by available scores." "UnrealAiToolKPolicy" "Core"
                toolDomainBias = component "Tool Domain Bias" "Maps editor snapshot/recent UI + optional tool_surface.domain_tags\nto a score multiplier; boost, not hard mask." "UnrealAiToolContextBias" "Core"
                toolUsagePrior = component "Operational Usage Prior" "Session-only ok/fail rates per tool_id; 0.7/0.3 blend default\n(ToolUsagePriorEnabled in UnrealAiRuntimeDefaults.h).\nOperational signal only, not user satisfaction." "UnrealAiToolUsagePrior" "Core"
                toolUsageEventLogger = component "Tool Usage Event Logger" "Append (query_hash, tool_id, operational_ok) JSONL for offline prior training\n(ToolUsageLogEnabled in UnrealAiRuntimeDefaults.h)." "UnrealAiToolUsageEventLogger" "Core"
                executionHost = component "Tool Execution Host" "Permission checks, dispatch invocation, result envelope shaping." "FUnrealAiToolExecutionHost" "Core"
                blueprintSurfaceGate = component "Blueprint surface gate + builder roster" "Main Agent: UnrealAiBlueprintToolGate + agent_surfaces omit graph mutators when bOmitMainAgentBlueprintMutationTools.\nBuilder: UnrealAiBlueprintBuilderToolSurface expands roster/appendix.\nHandoff: unreal_ai_build_blueprint tag + target_kind in harness." "UnrealAiBlueprintToolGate" "Core"
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

            retrievalService = container "Retrieval Service (Optional)" "Local vector index: embedding-first query (cosine Top-K in SQLite),\nlexical fallback; index builds are whitelist- and cap-driven (BYOK embeddings)." "C++" "RetrievalSection" {
                indexManager = component "Index lifecycle + orchestration" "BuildOrRebuildIndexNow: corpora, embeddings,\nphased waves (P0 project Source through P4 virtual), SQLite UpsertIncremental\nafter each wave; RemovedSources applied once at end; optional P0 time-budget\nmid-wave flush; manifest/diagnostics." "FUnrealAiRetrievalService" "RetrievalSection"
                indexPolicy = component "Index policy + filesystem crawl" "indexedExtensions whitelist; rootPreset (minimal/standard/extended);\nmax files/chunks/embeds; chunk size/overlap; embedding batch throttle;\nGetIndexBuildWaveForSource (wave buckets aligned with build-priority sort)." "UnrealAiRetrievalIndexConfig" "RetrievalSection"
                corpusFs = component "Filesystem text corpus" "Scans configured roots; whitelisted extensions only,\nopened as UTF-8 text (no raw binary asset reads)." "FUnrealAiRetrievalService" "RetrievalSection"
                corpusAr = component "Asset Registry metadata corpus" "Deterministic synthetic shards under virtual://asset_registry/* from FAssetData\n(names, classes, paths); caps + optional /Engine." "GatherAssetRegistryShardTexts" "RetrievalSection"
                corpusBp = component "Blueprint feature corpus" "Blueprint assets via registry-derived feature lines; extractor cap from settings." "UnrealAiBlueprintFeatureExtractor" "RetrievalSection"
                corpusMem = component "Memory corpus (optional)" "Off by default (indexMemoryRecordsInVectorStore).\nTagged memory service remains the primary memory UX." "CollectMemoryChunks" "RetrievalSection"
                queryEngine = component "Retrieval Query Engine" "Embed query text; cosine Top-K; empty-hit and circuit-breaker\npaths to lexical SQL fallback." "FUnrealAiRetrievalService" "RetrievalSection"
                migrationGuard = component "Model Compatibility Guard" "Embedding model mismatch, pending_reembed, fail-closed to deterministic context." "FUnrealAiRetrievalService" "Policy"
                store = component "Vector Index Store Adapter" "SQLite chunk rows + lexical index + manifest sidecar." "FUnrealAiVectorIndexStore" "RetrievalSection"
            }

            embeddingProvider = container "Embedding Provider (Optional)" "Embedding adapter interface + current OpenAI-compatible implementation." "C++" "RetrievalSection" {
                openAiCompat = component "OpenAI-Compatible Embedding Adapter" "Calls /embeddings and decodes vector arrays." "FOpenAiCompatibleEmbeddingProvider" "RetrievalSection"
            }

            observability = container "Observability + Diagnostics" "Decision logs, audits, workflow status,\ndiagnostics exports, and usage metrics." "C++" "Core" {
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
            toolUsageEventsJsonl = container "Saved/UnrealAi/tool_usage_events.jsonl" "Append-only operational tool usage lines for optional prior training (`query_hash`, `tool_id`, `operational_ok`)." "JSONL" "Data"
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
        plugin.requestBuilder.messageBudgeter -> plugin.tools.toolSurfacePipeline "Optional tiered tool index (Agent+dispatch+env)"
        plugin.tools.toolSurfacePipeline -> plugin.tools.toolQueryShaper "Heuristic + hybrid retrieval text"
        plugin.tools.toolSurfacePipeline -> plugin.tools.toolLexicalIndex "BM25 ranking over enabled tools"
        plugin.tools.toolSurfacePipeline -> plugin.tools.toolDomainBias "Editor snapshot â†’ tag multiplier"
        plugin.tools.toolSurfacePipeline -> plugin.tools.toolUsagePrior "Optional 0.7/0.3 blend"
        plugin.tools.toolSurfacePipeline -> plugin.tools.toolDynamicKPolicy "Margin-based K inside caps"
        plugin.tools.toolSurfacePipeline -> plugin.tools.catalogLoader "Tiered markdown + schemas"
        plugin.tools.toolSurfacePipeline -> plugin.tools.blueprintSurfaceGate "Filter eligible Blueprint tools (agent_surfaces)"
        plugin.tools.toolSurfacePipeline -> plugin.contextService "Read thread state for bias (not docs retrieval)"
        plugin.harness.workerOrchestration -> plugin.tools.blueprintSurfaceGate "Build-blueprint sub-turn handoff"
        plugin.harness.toolLoop -> plugin.tools.toolUsageEventLogger "Log operational_ok per invoke"
        plugin.harness.toolLoop -> plugin.tools.toolUsagePrior "Update session counts"
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
        plugin.retrievalService -> plugin.memoryService "Optional: read memory records when memory corpus enabled"
        plugin.retrievalService -> unrealEditor "Asset Registry + Blueprint reads for index corpora"
        plugin.retrievalService.indexManager -> plugin.retrievalService.indexPolicy "Resolves effective roots, whitelist, caps"
        plugin.retrievalService.indexManager -> plugin.retrievalService.corpusFs "Chunks changed files"
        plugin.retrievalService.indexManager -> plugin.retrievalService.corpusAr "Builds registry shards when enabled"
        plugin.retrievalService.indexManager -> plugin.retrievalService.corpusBp "Blueprint feature rows"
        plugin.retrievalService.indexManager -> plugin.retrievalService.corpusMem "Optional memory chunks"
        plugin.retrievalService.corpusFs -> plugin.retrievalService.indexPolicy "Extension + root filters"
        plugin.retrievalService.corpusAr -> unrealEditor "IAssetRegistry on game thread"
        plugin.retrievalService.corpusBp -> unrealEditor "Blueprint FAssetData"
        plugin.retrievalService.corpusMem -> plugin.memoryService "When gated on"
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
        plugin.dataRoot -> localData.toolUsageEventsJsonl "Owns"
        plugin.harness.toolLoop -> localData.toolUsageEventsJsonl "Append JSONL"

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

        component plugin.tools "tool-surface-graph" "Tool surface pipeline (dispatch eligibility).\nUnrealAiToolSurfacePipeline: query shaping, BM25 rank, editor domain bias,\noptional session usage prior, dynamic K, tiered markdown from FUnrealAiToolCatalog.\nNot the docs/project vector index (Retrieval Service). See docs/tooling/tools-expansion.md." {
            include plugin.tools.toolSurfacePipeline
            include plugin.tools.toolQueryShaper
            include plugin.tools.toolLexicalIndex
            include plugin.tools.toolDynamicKPolicy
            include plugin.tools.toolDomainBias
            include plugin.tools.toolUsagePrior
            include plugin.tools.toolUsageEventLogger
            include plugin.tools.catalogLoader
            include plugin.tools.blueprintSurfaceGate
            include plugin.requestBuilder.messageBudgeter
            include plugin.contextService.editorSnapshot
            include plugin.harness.toolLoop
            include localData.toolUsageEventsJsonl
            autoLayout tb
        }

        dynamic plugin "tool-surface-sequence" "Round 1 Agent + unreal_ai_dispatch.\nTurn builder may run eligibility before HTTP; tiered tool index in system message.\nHarness logs tool_surface_metrics, usage JSONL, session prior; optional repair nudge after bad dispatch unwrap.\nDynamic views here are container-level onlyâ€”see tool-surface-graph for\nMessageBudgeter to ToolSurfacePipeline to CatalogLoader." {
            plugin.requestBuilder -> plugin.tools "TryBuildTieredToolSurface + tiered markdown appendix"
            plugin.harness -> plugin.transport "Chat completion (compact tools[] + index)"
            plugin.harness -> plugin.tools "Tool loop: operational_ok, usage prior, tool_surface_metrics"
            plugin.harness -> localData.toolUsageEventsJsonl "Append JSONL line"
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

        container plugin "vector-db-end-to-end" "Optional local vector index (settings-driven whitelist, root presets, caps).\nCorpora: filesystem text, Asset Registry shards, Blueprint features,\noptional memory chunks. SQLite + manifest; BYOK embeddings; phased wave commits\nfor faster time-to-first-hit; merged into context ranker.\nDoes not index raw binary assets or full chat transcripts." {
            include plugin.retrievalService
            include plugin.embeddingProvider
            include plugin.contextService
            include plugin.harness
            include plugin.memoryService
            include localData.vectorDb
            include localData.vectorManifest
            include llmProvider
            include unrealEditor
            autoLayout tb
        }

        component plugin.retrievalService "vector-db-index-build" "Index rebuild (corpus rationale).\nPolicy from plugin_settings: whitelist extensions, root preset (minimal/standard/extended),\nper-rebuild caps/throttle to bound API, CPU, and disk.\nBuild commits in priority waves (project Source, plugin Source, Config+docs, Content, virtual);\nafter each wave chunks persist via UpsertIncremental; deletions deferred to final transaction;\noptional indexFirstWaveTimeBudgetSeconds for early P0 commits.\nFilesystem corpus: UTF-8 text for allow-listed paths only; binary Content not read as strings.\nAsset Registry: deterministic metadata shards (path, class, package) without .uasset bytes.\nBlueprint corpus: registry-derived feature lines.\nMemory corpus: optional, default-off (tagged memory stays primary).\nChat history is out of scope: `conversation.json` already feeds the harness; duplicating it in the index adds cost and overlap." {
            include *
            include plugin.embeddingProvider
            include localData.settingsJson
            include localData.vectorDb
            include localData.vectorManifest
            include unrealEditor
            include plugin.memoryService
            autoLayout tb
        }

        component plugin.contextService "vector-context-unified" "Unified vector-context graph for file system + scene context.\nSingle view: index build/query, L0/L1/L2 summaries, utility/head/tail scoring,\nand deterministic scene anchors merged in one rank/pack flow." {
            include plugin.contextService.sessionStore
            include plugin.contextService.editorSnapshot
            include plugin.contextService.candidateCollector
            include plugin.contextService.candidateScorer
            include plugin.contextService.packer
            include plugin.contextService.contextFormatter
            include plugin.contextService.contextJson

            include plugin.retrievalService.indexManager
            include plugin.retrievalService.indexPolicy
            include plugin.retrievalService.corpusFs
            include plugin.retrievalService.corpusAr
            include plugin.retrievalService.corpusBp
            include plugin.retrievalService.corpusMem
            include plugin.retrievalService.queryEngine
            include plugin.retrievalService.store
            include plugin.embeddingProvider.openAiCompat
            include plugin.observability.contextDecisionLogs

            include localData.vectorDb
            include localData.vectorManifest
            include localData.contextJson
            include localData.settingsJson
            include unrealEditor
            include llmProvider

            autoLayout lr
        }

        dynamic plugin "vector-db-query-sequence" "Per LLM round: optional retrieval prefetch.\nEmbed + SQLite query (or cache / lexical fallback).\nBuildContextWindow consumes snippets into ranked candidates." {
            plugin.harness -> plugin.contextService "StartRetrievalPrefetch (turn key)"
            plugin.requestBuilder -> plugin.contextService "BuildContextWindow"
            plugin.contextService -> plugin.retrievalService "TryConsumePrefetch / Query"
            plugin.retrievalService -> plugin.embeddingProvider "Embed query when not cached"
            plugin.embeddingProvider -> llmProvider "HTTPS /v1/embeddings (BYOK)"
            plugin.retrievalService -> localData.vectorDb "Cosine Top-K + lexical fallback SQL"
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


    /*
BEGIN_README_MAP system-context
**C4 system context (Level 1).** This is the outermost trust-boundary picture: who uses the assistant, what stays inside the editor process, and what leaves the machine.

- **Unreal Developer** drives the **UnrealAiEditor** plugin inside **Unreal Editor**; there is **no** required product backendâ€”only user-configured **HTTPS** to **third-party LLM APIs (BYOK)** and optional **localhost** bridges (MCP, CLI helpers) you opt into.
- **Unreal Editor Runtime** is the engine surface the tools actually call: selection, Asset Registry, Content Browser, PIE, tabs, and world state.
- **Local Data Store** holds settings, per-thread `conversation.json` and `context.json`, memories, optional SQLite vector files, diagnostics, and tool-usage JSONLâ€”everything stays under the plugin data root (see repo [`README.md`](README.md) and [`docs/tooling/agent-and-tool-requirements.md`](docs/tooling/agent-and-tool-requirements.md) section 1.4 (MVP deployment)).
END_README_MAP
BEGIN_README_MAP plugin-containers
**C4 containers (Level 2)** inside the `UnrealAiEditor` software system: the major runtime **modules** and how they wire together.

The **Backend Registry** is the composition root: it constructs **Model Profile Registry**, **Turn Request Builder**, **Agent Harness**, **LLM Transport**, **Tooling Runtime**, **Context Service**, **Memory Service**, optional **Retrieval** and **Embedding** adapters, and **Observability**. **UI (Slate)** talks to context + harness + policy for each send; **Policy** gates modes and confirmations. **Prompt Chunk Library** feeds the request builder; **Local Data Root** namespaces `%LOCALAPPDATA%/UnrealAiEditor`.

For how **context** vs **harness** split work, see [`docs/context/context-management.md`](docs/context/context-management.md) section 1.1; for plugin feature layout see [`Plugins/UnrealAiEditor/README.md`](Plugins/UnrealAiEditor/README.md).
END_README_MAP
BEGIN_README_MAP context-components
**Context subsystem** decomposition: thread-scoped state, **editor snapshot** queries, **@mention** resolution, **candidate** collection from attachments/tool snippets/memory/optional retrieval, **weighted ranking**, **budgeted packing**, **complexity assessor** output, and formatted blocks that become `BuildContextWindow` / `{{CONTEXT_SERVICE_OUTPUT}}` in the prompt.

Context owns **`context.json`** and planning artifacts that live beside it; it does **not** own the chat API message list (that is the **harness** + `conversation.json`). Optional local **vector retrieval** adds `retrieval_snippet` candidates into the **same** ranker when enabledâ€”see [`docs/context/context-management.md`](docs/context/context-management.md) and [`docs/context/vector-db-implementation-plan.md`](docs/context/vector-db-implementation-plan.md) section 2.2.
END_README_MAP
BEGIN_README_MAP harness-components
**Agent harness** decomposition: **turn loop**, **tool loop** (streaming tool calls, execution host, telemetry such as `tool_surface_metrics`, session **usage prior** updates, optional **repair** nudge after bad `unreal_ai_dispatch` unwrap), **continuation rails**, **Blueprint Builder handoff** (`unreal_ai_build_blueprint` + `target_kind` sub-turn), **Plan-mode DAG execution** (`Private/Planning/FUnrealAiPlanExecutor` driving serial node turns), and **run artifact** sinks (`FAgentRunFileSink`) for harness diagnostics.

This is where **`conversation.json`** is read/written, LLM rounds are bounded, and tool rounds connect to dispatch. For iteration, artifacts, and what â€œgoodâ€ looks like in tests, see [`docs/tooling/AGENT_HARNESS_HANDOFF.md`](docs/tooling/AGENT_HARNESS_HANDOFF.md).
END_README_MAP
BEGIN_README_MAP tooling-components
**Tool catalog, execution host, and dispatch** split by concern: **catalog loader** (`UnrealAiToolCatalog.json`), **tool surface pipeline** entry (for eligibility when enabled), **Blueprint surface gate** (`UnrealAiBlueprintToolGate` / builder roster), **execution host** (permissions + invocation), and **dispatch** modules (actors/world, assets, Blueprint, editor UI, search, PIE, etc.).

Narrowing **which tools appear** and **tiered markdown** for `unreal_ai_dispatch` is a separate pipeline from **docs vector retrieval**â€”see [`docs/tooling/tools-expansion.md`](docs/tooling/tools-expansion.md) and the companion view **Tool surface graph**. Narrative catalog: [`docs/tooling/tool-registry.md`](docs/tooling/tool-registry.md).
END_README_MAP
BEGIN_README_MAP tool-surface-graph
**Tool surface pipeline** (dispatch eligibility): **not** the project vector index. On Agent + dispatch rounds, `UnrealAiTurnLlmRequestBuilder` may call **`TryBuildTieredToolSurface`** before HTTP. **`UnrealAiToolSurfacePipeline`** composes **query shaping** (cheap heuristic + hybrid string), **BM25** over enabled tool text, optional **editor domain bias**, optional **session usage prior** (operational ok/fail blend), **dynamic K** from score margins, then **tiered markdown** from **`FUnrealAiToolCatalog`** under a hard character budget.

Telemetry (`tool_surface_metrics`) and optional **`tool_usage_events.jsonl`** support offline tuning. Full strategy and separation of concerns: [`docs/tooling/tools-expansion.md`](docs/tooling/tools-expansion.md); runtime toggles in **`UnrealAiRuntimeDefaults.h`** (see [`context.md`](context.md) in repo root for handoff notes).
END_README_MAP
BEGIN_README_MAP tool-surface-sequence
**Simplified dynamic sequence** for round 1: message budgeter / tiered surface â†’ compact **`tools[]` + markdown index** â†’ **chat completion** stream â†’ **tool loop** (operational_ok, usage prior, `tool_surface_metrics`) â†’ append **JSONL** usage line.

**Dynamic** diagram views cannot attach to arbitrary components at software-system scope, so the **full** internal wiring appears in **Tool surface graph**; this diagram is the **stage-to-stage** story aligned with the view description on `tool-surface-sequence` in `architecture.dsl`.
END_README_MAP
BEGIN_README_MAP retrieval-components
**Optional retrieval service** internals: **index lifecycle** (`BuildOrRebuildIndexNow`) with **phased embedding/commits** (waves P0-P4 aligned with `UnrealAiRetrievalIndexConfig` priority), **policy** (whitelist extensions, root presets, caps, throttles, wave bucket helper), **corpora** (filesystem text, Asset Registry shards, Blueprint features, optional memory chunks), **embedding** path, **SQLite** store + manifest (`UpsertIncremental` per wave; removals after all waves), **query** engine (cosine Top-K, lexical fallback once rows exist), and **model compatibility** guard.

Retrieval is **off by default**; when disabled, behavior must match pre-retrieval deterministic context ([`docs/context/vector-db-implementation-plan.md`](docs/context/vector-db-implementation-plan.md) section 3). See also **Vector DB** views below for end-to-end and query sequences.
END_README_MAP
BEGIN_README_MAP memory-components
**Memory service** decomposition: **staged query** (title â†’ description â†’ body), **compaction** heuristics, **retention** and **tombstones** to avoid regeneration loops, and JSON persistence under **`memories/`**.

Memory is **isolated** from raw chat transcript storage; integration into prompts is via **explicit** context candidates. Definitive reference: [`docs/context/memory-system.md`](docs/context/memory-system.md).
END_README_MAP
BEGIN_README_MAP ui-components
**Slate UI** decomposition: **chat tab shell**, **composer** (send pipeline, modes), **settings** surfaces (providers, models, retrieval, memory), **transcript** widgets (markdown, tool cards, warnings), and **Quick Start / Debug** tabs.

This maps to **Window â†’ Unreal AI** and related entry points described in the repo [`README.md`](README.md) and [`Plugins/UnrealAiEditor/README.md`](Plugins/UnrealAiEditor/README.md).
END_README_MAP
BEGIN_README_MAP request-lifecycle
**Numbered turn lifecycle**: load thread and snapshot â†’ **RunTurn** â†’ assemble prompts and **BuildContextWindow** â†’ resolve **model profile** â†’ **send** request â†’ **stream** response â†’ **execute tools** â†’ record snippets â†’ **observability** â†’ persist **`conversation.json`** and **`context.json`**.

Use this view with [`docs/context/context-management.md`](docs/context/context-management.md) (context assembly) and [`docs/tooling/AGENT_HARNESS_HANDOFF.md`](docs/tooling/AGENT_HARNESS_HANDOFF.md) (harness behavior and logs).
END_README_MAP
BEGIN_README_MAP vector-db-end-to-end
**Container-level end-to-end** optional vector story: **Retrieval Service** + **Embedding Provider** + **Context Service** + **Harness** + **Memory** (optional corpus feed) + on-disk **SQLite** + **manifest** + **LLM provider** for `/embeddings` + **Unreal Editor** for Asset Registry and Blueprint-derived corpora.

Index builds use **phased wave commits** to SQLite (priority corpora first; deferred deletions) so context can retrieve partial results earlier when enabled.

Aligns with [`docs/context/vector-db-implementation-plan.md`](docs/context/vector-db-implementation-plan.md) section 2.1 (visual architecture diagrams) and section 2.2 (what is indexed vs excludedâ€”no full chat dump, no raw binary `.uasset` bytes).
END_README_MAP
BEGIN_README_MAP vector-db-index-build
**Index rebuild** rationale: settings-driven **whitelist** and **root presets** bound CPU, disk, and **BYOK embedding** API cost. **Phased commits** write high-priority corpus (e.g. project `Source/`) to SQLite before lower-priority waves finish, improving time-to-first-retrieval; **manifest** tracks wave progress when enabled. **Filesystem** corpus reads UTF-8 text for allow-listed extensions only; **Asset Registry** adds deterministic metadata shards; **Blueprint** corpus uses feature lines, not raw assets; **memory** chunks into the index are **optional** and default-off so **tagged memory** stays primary.

See the long view caption in `architecture.dsl` and [`docs/context/vector-db-implementation-plan.md`](docs/context/vector-db-implementation-plan.md) sections 2.2â€“2.3 and phased indexing notes.
END_README_MAP
BEGIN_README_MAP vector-db-query-sequence
**Per-LLM-round query path**: harness may **prefetch** retrieval; **`BuildContextWindow`** consumes **TryConsumePrefetch** or **Query**; **embed** query when needed (BYOK **HTTPS**); **SQLite** cosine Top-K with **lexical fallback**; snippets feed the same **context ranker** as other candidate types.

Described in [`docs/context/vector-db-implementation-plan.md`](docs/context/vector-db-implementation-plan.md) section 2.1 table (`vector-db-query-sequence`) and retrieval sections of [`docs/context/context-management.md`](docs/context/context-management.md).
END_README_MAP
BEGIN_README_MAP vector-context-unified
**Unified vector-context graph** (single diagram): both **file-system vector context** and **scene/editor context** converge through one candidate ranking + packing pipeline.

- **File-system side**: retrieval index lifecycle and corpora (filesystem text, Asset Registry shards, Blueprint features, optional memory), embedding adapter, SQLite vector store + manifest.
- **Scene side**: deterministic live anchors from editor snapshot (selection, Content Browser focus, open editors) entering the same candidate flow.
- **Merge point**: candidate collection, scoring, and budget packing with utility/head/tail behavior and representation levels (L0/L1/L2).
- **Output and diagnostics**: formatted context + persisted context JSON + decision logs.
END_README_MAP
    */
}
