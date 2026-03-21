#pragma once

#include "CoreMinimal.h"
#include "Harness/ILlmTransport.h"

/**
 * Thin façade over ILlmTransport — posts the assembled `FUnrealAiLlmRequest` to the configured HTTP endpoint.
 * Pair with `UnrealAiTurnLlmRequestBuilder` (build prompt) + `FOpenAiCompatibleHttpTransport` (execute).
 */
class FUnrealAiLlmInvocationService
{
public:
	explicit FUnrealAiLlmInvocationService(TSharedPtr<ILlmTransport> InTransport)
		: Transport(MoveTemp(InTransport))
	{
	}

	void SubmitStreamChatCompletion(const FUnrealAiLlmRequest& Request, FUnrealAiLlmStreamCallback Callback) const
	{
		if (Transport.IsValid())
		{
			Transport->StreamChatCompletion(Request, Callback);
		}
	}

	void CancelActiveRequest() const
	{
		if (Transport.IsValid())
		{
			Transport->CancelActiveRequest();
		}
	}

	TSharedPtr<ILlmTransport> GetTransport() const { return Transport; }

private:
	TSharedPtr<ILlmTransport> Transport;
};
