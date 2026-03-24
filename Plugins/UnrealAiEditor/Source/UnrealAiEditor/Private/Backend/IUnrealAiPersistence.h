#pragma once

#include "CoreMinimal.h"

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

	/** Per-thread context store: `chats/<ProjectId>/threads/<ThreadId>/context.json`. */
	virtual bool SaveThreadContextJson(const FString& ProjectId, const FString& ThreadId, const FString& JsonBody) = 0;
	virtual bool LoadThreadContextJson(const FString& ProjectId, const FString& ThreadId, FString& OutJsonBody) = 0;

	/** Per-thread chat transcript for the harness: `chats/<ProjectId>/threads/<ThreadId>/conversation.json`. */
	virtual bool SaveThreadConversationJson(const FString& ProjectId, const FString& ThreadId, const FString& JsonBody) = 0;
	virtual bool LoadThreadConversationJson(const FString& ProjectId, const FString& ThreadId, FString& OutJsonBody) = 0;

	/**
	 * Optional: rename a chat on disk using a model-proposed name.
	 * This may move the per-thread folder (e.g. `threads/<ThreadId>` -> `threads/<slug>`).
	 */
	virtual bool SetThreadChatName(const FString& ProjectId, const FString& ThreadId, const FString& ChatName) = 0;
	virtual bool GetThreadChatName(const FString& ProjectId, const FString& ThreadId, FString& OutChatName) = 0;
};
