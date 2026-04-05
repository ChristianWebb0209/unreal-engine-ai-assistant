#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

struct FUnrealAiLlmRequest;

/**
 * TPM sliding-window token budgets (defaults in UnrealAiRuntimeDefaults.h)
 * for long-running harness batches. Strict mode (default when TPM is enabled) uses pessimistic
 * per-request upper bounds so Sum + estimate stays within budget — reduces 429s at the cost of wall time.
 */
namespace UnrealAiHarnessTpmThrottle
{
	/** Conservative token footprint for the next chat completion (prompt estimate + max output cap). */
	int32 EstimateChatFootprintTokens(const FUnrealAiLlmRequest& Req, int32 CharPerTokenApprox);

	/**
	 * True if MaybeWaitBeforeChatRequest would sleep (window over cap with non-empty event window).
	 * Instant checks only; call from game thread.
	 */
	bool WouldWaitBeforeChatRequest(const FUnrealAiLlmRequest& Req, int32 CharPerTokenApprox);

	/**
	 * Runs MinDelayWallSeconds sleep (if > 0) and chat TPM admission wait on a worker thread, then invokes
	 * OnGameThreadContinue on the game thread. Use when WouldWaitBeforeChatRequest is true or min-delay pacing applies.
	 */
	void WaitForChatAdmissionAsync(
		double MinDelayWallSeconds,
		const FUnrealAiLlmRequest& Req,
		int32 CharPerTokenApprox,
		TFunction<void()> OnGameThreadContinue);

	/** Block until the chat window has room for EstimateTokens (no-op if disabled). */
	void MaybeWaitBeforeChatRequest(const FUnrealAiLlmRequest& Req, int32 CharPerTokenApprox);

	/** Record actual total tokens from a completed chat round (no-op if disabled or TotalTokens <= 0). */
	void RecordChatCompletionTokens(int32 TotalTokens);

	/** Block until the embedding window has room (no-op if disabled). Pass UTF-16 input length (e.g. Request.InputText.Len()). */
	void MaybeWaitBeforeEmbeddingRequest(int32 InputUtf16CharCount);

	/**
	 * Single WaitForWindowRoom using the sum of per-input token estimates (same rules as MaybeWaitBeforeEmbeddingRequest).
	 * Use before a batched `/embeddings` HTTP call when TPM is enabled.
	 */
	void MaybeWaitBeforeEmbeddingBatchRequest(const TArray<FString>& InputTexts);

	/** Record actual tokens from a successful embeddings response (no-op if disabled or <= 0). */
	void RecordEmbeddingTokens(int32 TotalTokens);
}
