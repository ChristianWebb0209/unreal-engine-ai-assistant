#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"
#include "Misc/UnrealAiWaitTimePolicy.h"
#include "UnrealAiBlueprintBuilderTargetKind.h"
#include "UnrealAiProductSpecialistId.h"

/** Stable run identifiers for observability (parent/child workers). */
struct FUnrealAiRunIds
{
	FGuid RunId;
	FGuid ParentRunId;
	int32 WorkerIndex = INDEX_NONE;
};

/** Capability profile — harness logic uses this, not raw model name strings. */
struct FUnrealAiModelCapabilities
{
	FString ModelIdForApi;
	/** Named provider from `providers[]` in settings; empty = legacy `api` block or default provider. */
	FString ProviderId;
	int32 MaxContextTokens = 128000;
	int32 MaxOutputTokens = 4096;
	bool bSupportsNativeTools = true;
	bool bSupportsParallelToolCalls = true;
	/** When false, image-like context attachments are omitted from the built context (text-only models). */
	bool bSupportsImages = true;
	/**
	 * Soft backstop: max tool↔LLM iterations per user send (each round = one completion, possibly + tool calls).
	 * Primary limits are repeated identical tool failures, consecutive identical successful no-progress tools (agent harness), and MaxAgentTurnTokens; this remains a safety ceiling (clamped to 1 through UnrealAiWaitTime::AgentMaxLlmRoundsHardCap).
	 */
	int32 MaxAgentLlmRounds = UnrealAiWaitTime::AgentDefaultMaxLlmRounds;
	/**
	 * Max total prompt+completion tokens for one agent turn (one user message / RunTurn).
	 * Negative = unlimited; 0 = harness default cap; positive = hard limit.
	 */
	int32 MaxAgentTurnTokens = 0;
};

/** Single function/tool call from the model (chat-completions tool_calls shape). */
struct FUnrealAiToolCallSpec
{
	FString Id;
	FString Name;
	FString ArgumentsJson;
	/** SSE deltas only: merge fragments with the same index into one spec. INDEX_NONE = full spec (non-stream). */
	int32 StreamMergeIndex = INDEX_NONE;
};

/** One message in the persisted conversation / API payload. */
struct FUnrealAiConversationMessage
{
	FString Role;
	FString Content;
	TArray<FUnrealAiToolCallSpec> ToolCalls;
	FString ToolCallId;
	/**
	 * When Role is "user": mode active when the message was sent (UI + conversation.json only).
	 * Never included in LLM API payloads.
	 */
	bool bHasUserAgentMode = false;
	EUnrealAiAgentMode UserAgentMode = EUnrealAiAgentMode::Agent;
	/** HTTP API only (not persisted): OpenAI-style image_url data URLs for multimodal user turns. */
	TArray<FString> VisionImageDataUrls;
};

enum class EUnrealAiLlmStreamEventType : uint8
{
	AssistantDelta,
	ThinkingDelta,
	ToolCalls,
	Finish,
	Error,
};

struct FUnrealAiTokenUsage
{
	int32 PromptTokens = 0;
	int32 CompletionTokens = 0;
	int32 TotalTokens = 0;
};

/** Normalized stream event from ILlmTransport (provider-agnostic). */
struct FUnrealAiLlmStreamEvent
{
	EUnrealAiLlmStreamEventType Type = EUnrealAiLlmStreamEventType::AssistantDelta;
	FString DeltaText;
	TArray<FUnrealAiToolCallSpec> ToolCalls;
	FString FinishReason;
	FUnrealAiTokenUsage Usage;
	FString ErrorMessage;
};

/** One row in the final tool roster order (guardrails first, then top‑K by combined score). */
struct FUnrealAiToolSurfaceRankedEntry
{
	int32 Rank = 0;
	FString ToolId;
	float CombinedScore = 0.f;
	/** BM25 similarity vs hybrid query, normalized by max score in pool (0–1). */
	float Bm25Norm01 = 0.f;
	/** Domain tag context multiplier from UnrealAiToolContextBias. */
	float ContextMultiplier = 1.f;
	/** Operational usage prior in [0,1] (same signal blended when usage prior is enabled). */
	float UsagePrior01 = 0.f;
	bool bUsagePriorBlended = false;
	bool bGuardrail = false;
	/** Short human-readable why this row appears (guardrail vs score-ranked). */
	FString SelectionReason;
};

/** Observability for tool surface assembly (tools-expansion.md §2.8). */
struct FUnrealAiToolSurfaceTelemetry
{
	/** off | dispatch_eligibility | orchestrator_allow_list | product_specialist_allow_list | native_eligibility */
	FString ToolSurfaceMode;
	int32 EligibleCount = 0;
	int32 RosterChars = 0;
	/** First N tools that receive expanded appendix entries (see UnrealAiRuntimeDefaults::ToolExpandedCount); subset of RankedTools. */
	TArray<FString> ExpandedToolIds;
	int32 BudgetRemaining = 0;
	int32 RetrievalLatencyMs = 0;
	int32 KEffective = 0;
	/** raw | heuristic | hybrid */
	FString QueryShape;
	/** Fingerprint of hybrid retrieval query for usage logging (may be empty when eligibility off). */
	FString QueryHash;
	/** Full ordered roster passed to the tiered index (guardrails + top‑K), with per-tool scoring breakdown. */
	TArray<FUnrealAiToolSurfaceRankedEntry> RankedTools;
	/** e.g. default | blueprint_builder_verbose | environment_builder_verbose */
	FString SurfaceProfile;
	/** Blueprint Builder: max chars allowed for the tiered tool appendix before catalog trim (0 = not builder / default path). */
	int32 AppendixCharBudgetLimit = 0;
};

/** Request into the harness from UI / tabs. */
struct FUnrealAiAgentTurnRequest
{
	FString ProjectId;
	FString ThreadId;
	EUnrealAiAgentMode Mode = EUnrealAiAgentMode::Agent;
	FString UserText;
	/**
	 * When non-empty, used for retrieval, complexity heuristics, tool-index shaping, and prefetch instead of UserText.
	 * Plan-node turns leave this empty and rely on UserText (full original request + node block from FUnrealAiPlanExecutor).
	 */
	FString ContextComplexityUserText;
	/** Logical profile id (e.g. settings key); resolved to FUnrealAiModelCapabilities. */
	FString ModelProfileId;
	/** If true, record assistant output as tool result for context demos (compat with stub). */
	bool bRecordAssistantAsStubToolResult = false;
	/**
	 * When > 0, the harness uses max(profile MaxAgentLlmRounds, this) for this turn.
	 * Plan worker nodes set this so multi-tool steps are not cut off at the profile default.
	 */
	int32 LlmRoundBudgetFloor = 0;
	/** Skip dispatch + tiered tool index; emit full per-tool JSON (headless tests / diagnostics). */
	bool bForceNativeToolSurface = false;

	/**
	 * When true, this turn uses Blueprint Builder prompts and receives the full Blueprint mutation tool surface.
	 * Set by the harness when chaining after `<unreal_ai_build_blueprint>...</unreal_ai_build_blueprint>` from the main agent.
	 */
	bool bBlueprintBuilderTurn = false;

	/**
	 * Parsed from handoff frontmatter (`target_kind:`) for the active builder sub-turn.
	 * Drives conditional prompt chunks + `builder_domains` tool merging. Reset when the builder result is consumed.
	 */
	EUnrealAiBlueprintBuilderTargetKind BlueprintBuilderTargetKind = EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;

	/**
	 * When true (default) on Agent turns that are NOT a Builder sub-turn, catalog-gated Blueprint graph mutators
	 * are omitted from the tiered tool index — use `<unreal_ai_build_blueprint>` instead.
	 */
	bool bOmitMainAgentBlueprintMutationTools = true;

	/**
	 * One-shot: after `<unreal_ai_blueprint_builder_result>`, inject `blueprint-builder/09-resume-on-main-agent.md` into the main-agent system prompt once.
	 * Cleared when UnrealAiTurnLlmRequestBuilder::Build consumes it.
	 */
	bool bInjectBlueprintBuilderResumeChunk = false;

	/**
	 * Product specialist sub-turn (e.g. Scene) after `<unreal_ai_delegate specialist="...">` from the orchestrator.
	 * Cleared when `<unreal_ai_specialist_result>` is consumed.
	 */
	EUnrealAiProductSpecialistId ActiveProductSpecialistId = EUnrealAiProductSpecialistId::None;

	/**
	 * Inner payload from the last `<unreal_ai_delegate>...</unreal_ai_delegate>` (verbatim, bounded).
	 * Injected into specialist system prompts as {{SPECIALIST_DELEGATION_BRIEF}}; cleared when the specialist returns or on builder handoff.
	 */
	FString LastProductSpecialistDelegationBrief;

	/** One-shot: after specialist result, inject `specialists/00-resume-to-orchestrator.md` into the orchestrator stack. */
	bool bInjectProductSpecialistResumeChunk = false;

	/**
	 * Thin Agent orchestrator: delegation prompts + fixed read-mostly tool roster.
	 * Excludes builder sub-turns, product specialist sub-turns, and plan DAG worker threads (`*_plan_*`).
	 */
	bool IsOrchestratorAgentToolSurface() const
	{
		return Mode == EUnrealAiAgentMode::Agent && !bBlueprintBuilderTurn
			&& ActiveProductSpecialistId == EUnrealAiProductSpecialistId::None && !ThreadId.Contains(TEXT("_plan_"));
	}
};
