#pragma once

#include "CoreMinimal.h"
#include "Harness/ILlmTransport.h"

/**
 * Thin façade over ILlmTransport — submits the assembled `FUnrealAiLlmRequest`.
 * Pair with `UnrealAiTurnLlmRequestBuilder`; inject any transport that speaks your provider’s wire format.
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
