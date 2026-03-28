#pragma once

#include "Backend/IUnrealAiPersistence.h"

class FUnrealAiPersistenceStub final : public IUnrealAiPersistence
{
public:
	FUnrealAiPersistenceStub();

	virtual FString GetDataRootDirectory() const override;
	virtual bool SaveSettingsJson(const FString& Json) override;
	virtual bool LoadSettingsJson(FString& OutJson) override;
	virtual bool SaveUsageStatsJson(const FString& Json) override;
	virtual bool LoadUsageStatsJson(FString& OutJson) override;
	virtual bool AppendChatMessage(const FString& ProjectId, const FUnrealAiChatMessage& Message) override;
	virtual TArray<FString> ListThreads(const FString& ProjectId) override;
	virtual bool SaveThreadContextJson(const FString& ProjectId, const FString& ThreadId, const FString& JsonBody) override;
	virtual bool LoadThreadContextJson(const FString& ProjectId, const FString& ThreadId, FString& OutJsonBody) override;
	virtual bool SaveThreadConversationJson(const FString& ProjectId, const FString& ThreadId, const FString& JsonBody) override;
	virtual bool LoadThreadConversationJson(const FString& ProjectId, const FString& ThreadId, FString& OutJsonBody) override;
	virtual bool SaveThreadPlanDraftJson(const FString& ProjectId, const FString& ThreadId, const FString& JsonBody) override;
	virtual bool LoadThreadPlanDraftJson(const FString& ProjectId, const FString& ThreadId, FString& OutJsonBody) override;
	virtual void ClearThreadPlanDraft(const FString& ProjectId, const FString& ThreadId) override;

	virtual FString GetThreadStorageSlug(const FString& ProjectId, const FString& ThreadId) const override;

	virtual bool SetThreadChatName(const FString& ProjectId, const FString& ThreadId, const FString& ChatName) override;
	virtual bool GetThreadChatName(const FString& ProjectId, const FString& ThreadId, FString& OutChatName) override;
	virtual void ForgetThread(const FString& ProjectId, const FString& ThreadId) override;
	virtual bool SaveOpenChatTabsState(const FString& ProjectId, const TArray<FGuid>& OpenThreadIdsInOrder) override;
	virtual bool LoadOpenChatTabsState(const FString& ProjectId, TArray<FGuid>& OutOpenThreadIdsInOrder) override;
	virtual bool SaveProjectRecentUiJson(const FString& ProjectId, const FString& JsonBody) override;
	virtual bool LoadProjectRecentUiJson(const FString& ProjectId, FString& OutJsonBody) override;
	virtual bool SaveMemoryIndexJson(const FString& JsonBody) override;
	virtual bool LoadMemoryIndexJson(FString& OutJsonBody) override;
	virtual bool SaveMemoryTombstonesJson(const FString& JsonBody) override;
	virtual bool LoadMemoryTombstonesJson(FString& OutJsonBody) override;
	virtual bool SaveMemoryItemJson(const FString& MemoryId, const FString& JsonBody) override;
	virtual bool LoadMemoryItemJson(const FString& MemoryId, FString& OutJsonBody) override;
	virtual bool DeleteMemoryItemJson(const FString& MemoryId) override;
	virtual void ListMemoryItemIds(TArray<FString>& OutMemoryIds) const override;
	virtual bool SaveMemoryGenerationStatusJson(const FString& JsonBody) override;
	virtual bool LoadMemoryGenerationStatusJson(FString& OutJsonBody) override;
	virtual void ListPersistedThreadsForHistory(
		const FString& ProjectId,
		TArray<FString>& OutThreadIds,
		TArray<FString>& OutDisplayNames) const override;
	virtual bool ThreadHasUserVisibleMessages(const FString& ProjectId, const FString& ThreadId) const override;
	virtual bool TryGetMostRecentThreadWithConversation(const FString& ProjectId, FGuid& OutThreadId) const override;
	virtual bool DeleteAllLocalChatData(FString& OutError) override;

private:
	FString EnsureSubdir(const FString& RelativePath) const;
	bool LoadThreadConversationJson_Impl(
		const FString& ProjectId,
		const FString& ThreadId,
		FString& OutJsonBody) const;
};
