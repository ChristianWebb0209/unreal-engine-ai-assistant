#pragma once

#include "CoreMinimal.h"

struct FUnrealAiLlmRequest;

/**
 * Opt-in (UNREAL_AI_HARNESS_TPM_PER_MINUTE / embedding env) sliding-window token budgets
 * for long-running harness batches. Strict mode (default when TPM is enabled) uses pessimistic
 * per-request upper bounds so Sum + estimate stays within budget — reduces 429s at the cost of wall time.
 */
namespace UnrealAiHarnessTpmThrottle
{
	/** Conservative token footprint for the next chat completion (prompt estimate + max output cap). */
	int32 EstimateChatFootprintTokens(const FUnrealAiLlmRequest& Req, int32 CharPerTokenApprox);

	/** Block until the chat window has room for EstimateTokens (no-op if disabled). */
	void MaybeWaitBeforeChatRequest(const FUnrealAiLlmRequest& Req, int32 CharPerTokenApprox);

	/** Record actual total tokens from a completed chat round (no-op if disabled or TotalTokens <= 0). */
	void RecordChatCompletionTokens(int32 TotalTokens);

	/** Block until the embedding window has room (no-op if disabled). Pass UTF-16 input length (e.g. Request.InputText.Len()). */
	void MaybeWaitBeforeEmbeddingRequest(int32 InputUtf16CharCount);

	/** Record actual tokens from a successful embeddings response (no-op if disabled or <= 0). */
	void RecordEmbeddingTokens(int32 TotalTokens);
}
