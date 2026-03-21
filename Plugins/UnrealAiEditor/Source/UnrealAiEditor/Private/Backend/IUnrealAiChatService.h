#pragma once

#include "CoreMinimal.h"

DECLARE_DELEGATE(FUnrealAiChatSimpleDelegate);
DECLARE_DELEGATE_OneParam(FUnrealAiChatTokenDelegate, const FString& /*TokenChunk*/);
DECLARE_DELEGATE(FUnrealAiChatCompleteDelegate);

/** Optional per-send metadata for context service + stub hooks. */
struct FUnrealAiChatSendMeta
{
	FString ProjectId;
	FString ThreadId;
	/** If true, stub records the full assistant text as a fake tool result for context persistence demos. */
	bool bRecordStubToolResult = false;
};

class IUnrealAiChatService
{
public:
	virtual ~IUnrealAiChatService() = default;

	virtual void Connect(FUnrealAiChatSimpleDelegate OnConnected) = 0;
	virtual void Disconnect() = 0;
	virtual bool IsConnected() const = 0;
	virtual void SendMessage(
		const FString& Prompt,
		FUnrealAiChatTokenDelegate OnToken,
		FUnrealAiChatCompleteDelegate OnComplete,
		const FUnrealAiChatSendMeta* Meta = nullptr) = 0;
};
