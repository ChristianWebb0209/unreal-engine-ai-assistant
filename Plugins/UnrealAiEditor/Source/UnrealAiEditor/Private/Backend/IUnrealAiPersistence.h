#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

struct FUnrealAiChatMessage
{
	FString Role;
	FString Content;
};

class IUnrealAiPersistence
{
public:
	virtual ~IUnrealAiPersistence() = default;

	virtual FString GetDataRootDirectory() const = 0;
	virtual bool SaveSettingsJson(const FString& Json) = 0;
	virtual bool LoadSettingsJson(FString& OutJson) = 0;

	/** Cumulative token usage / rough spend estimates: `settings/usage_stats.json`. */
	virtual bool SaveUsageStatsJson(const FString& Json) = 0;
	virtual bool LoadUsageStatsJson(FString& OutJson) = 0;
	virtual bool AppendChatMessage(const FString& ProjectId, const FUnrealAiChatMessage& Message) = 0;
	virtual TArray<FString> ListThreads(const FString& ProjectId) = 0;

	/**
	 * Per-thread context as `chats/<ProjectId>/threads/<slug>-context.json`
	 * (slug is thread GUID until renamed; legacy `threads/<slug>/context.json` is still read).
	 */
	virtual bool SaveThreadContextJson(const FString& ProjectId, const FString& ThreadId, const FString& JsonBody) = 0;
	virtual bool LoadThreadContextJson(const FString& ProjectId, const FString& ThreadId, FString& OutJsonBody) = 0;

	/** Per-thread harness transcript: `chats/<ProjectId>/threads/<slug>-conversation.json`. */
	virtual bool SaveThreadConversationJson(const FString& ProjectId, const FString& ThreadId, const FString& JsonBody) = 0;
	virtual bool LoadThreadConversationJson(const FString& ProjectId, const FString& ThreadId, FString& OutJsonBody) = 0;

	/** Storage key (slug) used in thread filenames; GUID string until `SetThreadChatName`. */
	virtual FString GetThreadStorageSlug(const FString& ProjectId, const FString& ThreadId) const = 0;

	/**
	 * Optional: rename a chat on disk using a model-proposed name (updates slug; renames `*-context` / `*-conversation` files).
	 */
	virtual bool SetThreadChatName(const FString& ProjectId, const FString& ThreadId, const FString& ChatName) = 0;
	virtual bool GetThreadChatName(const FString& ProjectId, const FString& ThreadId, FString& OutChatName) = 0;

	/** Remove per-thread persisted JSON and index entry (e.g. when deleting a chat in the UI). */
	virtual void ForgetThread(const FString& ProjectId, const FString& ThreadId) = 0;

	/**
	 * Persist which Agent Chat dock tabs were open (thread GUIDs, left-to-right creation order).
	 * Stored under `chats/<ProjectId>/ui_open_chats.json`.
	 */
	virtual bool SaveOpenChatTabsState(const FString& ProjectId, const TArray<FGuid>& OpenThreadIdsInOrder) = 0;
	virtual bool LoadOpenChatTabsState(const FString& ProjectId, TArray<FGuid>& OutOpenThreadIdsInOrder) = 0;
	/** Project-global recent UI history blob (`chats/<ProjectId>/recent-ui.json`). */
	virtual bool SaveProjectRecentUiJson(const FString& ProjectId, const FString& JsonBody) = 0;
	virtual bool LoadProjectRecentUiJson(const FString& ProjectId, FString& OutJsonBody) = 0;
	/** Memory index (`memories/index.json`). */
	virtual bool SaveMemoryIndexJson(const FString& JsonBody) = 0;
	virtual bool LoadMemoryIndexJson(FString& OutJsonBody) = 0;
	/** Memory tombstones (`memories/tombstones.json`). */
	virtual bool SaveMemoryTombstonesJson(const FString& JsonBody) = 0;
	virtual bool LoadMemoryTombstonesJson(FString& OutJsonBody) = 0;
	/** Memory item payload (`memories/items/<MemoryId>.json`). */
	virtual bool SaveMemoryItemJson(const FString& MemoryId, const FString& JsonBody) = 0;
	virtual bool LoadMemoryItemJson(const FString& MemoryId, FString& OutJsonBody) = 0;
	virtual bool DeleteMemoryItemJson(const FString& MemoryId) = 0;
	virtual void ListMemoryItemIds(TArray<FString>& OutMemoryIds) const = 0;
	/** Memory generation health (`memories/generation_status.json`). */
	virtual bool SaveMemoryGenerationStatusJson(const FString& JsonBody) = 0;
	virtual bool LoadMemoryGenerationStatusJson(FString& OutJsonBody) = 0;

	/** Known persisted threads for history UI (from threads_index.json). */
	virtual void ListPersistedThreadsForHistory(const FString& ProjectId, TArray<FString>& OutThreadIds, TArray<FString>& OutDisplayNames) const = 0;

	/**
	 * True if persisted thread conversation exists and parses to at least one non-system message
	 * (user / assistant / tool).
	 */
	virtual bool ThreadHasUserVisibleMessages(const FString& ProjectId, const FString& ThreadId) const = 0;

	/** Most recently modified thread on disk that passes ThreadHasUserVisibleMessages (if any). */
	virtual bool TryGetMostRecentThreadWithConversation(const FString& ProjectId, FGuid& OutThreadId) const = 0;

	/**
	 * Deletes all chat/thread data under the local data root (`chats/`) and project `Saved/UnrealAiEditor/`.
	 * Preserves `settings/` (plugin_settings.json, usage_stats.json, model profiles, etc.).
	 */
	virtual bool DeleteAllLocalChatData(FString& OutError) = 0;
};
