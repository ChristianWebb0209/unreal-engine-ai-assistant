#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"

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
	 * Primary limits are repeated identical tool failures and MaxAgentTurnTokens; this remains a safety ceiling (clamped e.g. 1–512).
	 */
	int32 MaxAgentLlmRounds = 512;
	/**
	 * Max total prompt+completion tokens for one agent turn (one user message / RunTurn). 0 = use harness default
	 * or UNREAL_AI_HARNESS_MAX_TOKENS_PER_TURN. Set to -1 in JSON via a sentinel if we add UI later; env "0" disables the cap.
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

/** Observability for tool surface assembly (tools-expansion.md §2.8). */
struct FUnrealAiToolSurfaceTelemetry
{
	/** off | dispatch_eligibility | native_eligibility */
	FString ToolSurfaceMode;
	int32 EligibleCount = 0;
	int32 RosterChars = 0;
	TArray<FString> ExpandedToolIds;
	int32 BudgetRemaining = 0;
	int32 RetrievalLatencyMs = 0;
	int32 KEffective = 0;
	/** raw | heuristic | hybrid */
	FString QueryShape;
	/** Fingerprint of hybrid retrieval query for usage logging (may be empty when eligibility off). */
	FString QueryHash;
};

/** Request into the harness from UI / tabs. */
struct FUnrealAiAgentTurnRequest
{
	FString ProjectId;
	FString ThreadId;
	EUnrealAiAgentMode Mode = EUnrealAiAgentMode::Agent;
	FString UserText;
	/** Logical profile id (e.g. settings key); resolved to FUnrealAiModelCapabilities. */
	FString ModelProfileId;
	/** If true, record assistant output as tool result for context demos (compat with stub). */
	bool bRecordAssistantAsStubToolResult = false;
	/**
	 * When > 0, the harness uses max(profile MaxAgentLlmRounds, this) for this turn.
	 * Plan worker nodes set this so multi-tool steps are not cut off at the profile default.
	 */
	int32 LlmRoundBudgetFloor = 0;
};

/** Structured result from a Level-B worker (parent merge consumes this). */
struct FUnrealAiWorkerResult
{
	FString Status;
	FString Summary;
	TArray<FString> Artifacts;
	TArray<FString> Errors;
	TArray<FString> FollowUpQuestions;
};
