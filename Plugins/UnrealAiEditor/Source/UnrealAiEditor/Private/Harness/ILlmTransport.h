#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

DECLARE_DELEGATE_OneParam(FUnrealAiLlmStreamCallback, const FUnrealAiLlmStreamEvent& /*Event*/);

/** Request payload for HTTPS chat/completions-style endpoints (multiple vendors, same JSON shape). No tool execution here. */
struct FUnrealAiLlmRequest
{
	FString ApiModelName;
	TArray<FUnrealAiConversationMessage> Messages;
	/** Serialized JSON array of tool definitions, or empty when no tools. */
	FString ToolsJsonArray;
	bool bStream = true;
	int32 MaxOutputTokens = 4096;
	/** Resolved from model profile + providers (required for HTTP transport). */
	FString ApiBaseUrl;
	FString ApiKey;
	/** If > 0, overrides UnrealAiWaitTime::HttpRequestTimeoutSec for this request (e.g. Plan planner). */
	float HttpTimeoutOverrideSec = 0.f;
};

class ILlmTransport
{
public:
	virtual ~ILlmTransport() = default;

	virtual void StreamChatCompletion(
		const FUnrealAiLlmRequest& Request,
		FUnrealAiLlmStreamCallback OnEvent) = 0;

	virtual void CancelActiveRequest() {}

	/** True while an HTTP chat/completions request is in flight (streaming or waiting on response). */
	virtual bool HasActiveRequest() const { return false; }
};
