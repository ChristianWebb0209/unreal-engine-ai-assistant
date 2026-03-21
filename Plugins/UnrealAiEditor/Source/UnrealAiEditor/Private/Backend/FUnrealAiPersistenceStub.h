#pragma once

#include "Backend/IUnrealAiPersistence.h"

class FUnrealAiPersistenceStub final : public IUnrealAiPersistence
{
public:
	FUnrealAiPersistenceStub();

	virtual FString GetDataRootDirectory() const override;
	virtual bool SaveSettingsJson(const FString& Json) override;
	virtual bool LoadSettingsJson(FString& OutJson) override;
	virtual bool AppendChatMessage(const FString& ProjectId, const FUnrealAiChatMessage& Message) override;
	virtual TArray<FString> ListThreads(const FString& ProjectId) override;
	virtual bool SaveThreadContextJson(const FString& ProjectId, const FString& ThreadId, const FString& JsonBody) override;
	virtual bool LoadThreadContextJson(const FString& ProjectId, const FString& ThreadId, FString& OutJsonBody) override;
	virtual bool SaveThreadConversationJson(const FString& ProjectId, const FString& ThreadId, const FString& JsonBody) override;
	virtual bool LoadThreadConversationJson(const FString& ProjectId, const FString& ThreadId, FString& OutJsonBody) override;

private:
	FString EnsureSubdir(const FString& RelativePath) const;
};
