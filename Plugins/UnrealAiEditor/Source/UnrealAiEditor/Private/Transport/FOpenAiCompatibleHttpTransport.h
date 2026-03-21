#pragma once

#include "CoreMinimal.h"
#include "Harness/ILlmTransport.h"
#include "Interfaces/IHttpRequest.h"

/** HTTPS chat/completions — uses per-request ApiBaseUrl / ApiKey on FUnrealAiLlmRequest. */
class FOpenAiCompatibleHttpTransport final : public ILlmTransport
{
public:
	FOpenAiCompatibleHttpTransport() = default;

	virtual void StreamChatCompletion(const FUnrealAiLlmRequest& Request, FUnrealAiLlmStreamCallback OnEvent) override;
	virtual void CancelActiveRequest() override;

private:
	TSharedPtr<IHttpRequest> ActiveRequest;
};
