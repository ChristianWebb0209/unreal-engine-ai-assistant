#pragma once

#include "CoreMinimal.h"
#include "Harness/ILlmTransport.h"
#include "Interfaces/IHttpRequest.h"

/**
 * Default HTTPS transport: POST /v1/chat/completions (or equivalent) with SSE optional.
 * Works with any provider that accepts the common messages + tools JSON layout; not tied to one vendor.
 */
class FOpenAiCompatibleHttpTransport final : public ILlmTransport
{
public:
	FOpenAiCompatibleHttpTransport() = default;

	virtual void StreamChatCompletion(const FUnrealAiLlmRequest& Request, FUnrealAiLlmStreamCallback OnEvent) override;
	virtual void CancelActiveRequest() override;
	virtual bool HasActiveRequest() const override;

private:
	TSharedPtr<IHttpRequest> ActiveRequest;
};
