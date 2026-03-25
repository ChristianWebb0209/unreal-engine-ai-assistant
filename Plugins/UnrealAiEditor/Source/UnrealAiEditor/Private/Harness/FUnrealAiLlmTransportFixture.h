#pragma once

#include "CoreMinimal.h"
#include "Harness/ILlmTransport.h"

/**
 * Deterministic LLM transport driven by a JSON fixture file (for CI / harness testing).
 * Set env UNREAL_AI_LLM_FIXTURE to an absolute or project-relative path before starting the editor.
 *
 * Schema (see tests/harness_llm_fixture.schema.json):
 * { "responses": [ { "events": [ ... ] }, ... ] }
 * Each StreamChatCompletion consumes the next response; events are emitted in order via the game ticker.
 */
class FUnrealAiLlmTransportFixture final : public ILlmTransport, public TSharedFromThis<FUnrealAiLlmTransportFixture>
{
public:
	explicit FUnrealAiLlmTransportFixture(FString InFixturePath);

	virtual void StreamChatCompletion(const FUnrealAiLlmRequest& Request, FUnrealAiLlmStreamCallback OnEvent) override;
	virtual void CancelActiveRequest() override;

	const FString& GetFixturePath() const { return FixturePath; }

private:
	bool EnsureLoaded(FString& OutError);

	FString FixturePath;
	bool bLoaded = false;
	TArray<TArray<FUnrealAiLlmStreamEvent>> CachedResponses;
	FCriticalSection LoadMutex;
	std::atomic<bool> bCancelRequested{false};
	int32 NextResponseIndex = 0;
};
