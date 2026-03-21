#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

DECLARE_DELEGATE_OneParam(FUnrealAiLlmStreamCallback, const FUnrealAiLlmStreamEvent& /*Event*/);

/** OpenAI-compatible (and similar) chat completion — no tool execution here. */
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
};

class ILlmTransport
{
public:
	virtual ~ILlmTransport() = default;

	virtual void StreamChatCompletion(
		const FUnrealAiLlmRequest& Request,
		FUnrealAiLlmStreamCallback OnEvent) = 0;

	virtual void CancelActiveRequest() {}
};
