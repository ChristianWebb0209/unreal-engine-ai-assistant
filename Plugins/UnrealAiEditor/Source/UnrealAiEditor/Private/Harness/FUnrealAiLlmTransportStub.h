#pragma once

#include "CoreMinimal.h"
#include "Harness/ILlmTransport.h"

/** Offline transport: no network; emits a short assistant reply (for UI dev / missing API key). */
class FUnrealAiLlmTransportStub final : public ILlmTransport
{
public:
	virtual void StreamChatCompletion(const FUnrealAiLlmRequest& Request, FUnrealAiLlmStreamCallback OnEvent) override;
};
