#pragma once

#include "Backend/IUnrealAiChatService.h"

class FUnrealAiBackendRegistry;

class FUnrealAiChatServiceStub final : public IUnrealAiChatService
{
public:
	explicit FUnrealAiChatServiceStub(FUnrealAiBackendRegistry* InRegistry);

	virtual void Connect(FUnrealAiChatSimpleDelegate OnConnected) override;
	virtual void Disconnect() override;
	virtual bool IsConnected() const override;
	virtual void SendMessage(
		const FString& Prompt,
		FUnrealAiChatTokenDelegate OnToken,
		FUnrealAiChatCompleteDelegate OnComplete,
		const FUnrealAiChatSendMeta* Meta = nullptr) override;

private:
	FUnrealAiBackendRegistry* Registry = nullptr;
	bool bConnected = false;
};
