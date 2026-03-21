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
	 * Max tool↔LLM iterations per user send (each round = one completion, possibly + tool calls).
	 * Higher values allow longer agent runs but usually consume more tokens. Clamped at runtime (e.g. 1–256).
	 */
	int32 MaxAgentLlmRounds = 16;
};

/** Single tool invocation produced by the LLM (OpenAI-style). */
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
