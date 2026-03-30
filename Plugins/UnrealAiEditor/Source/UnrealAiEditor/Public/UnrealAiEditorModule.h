#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Misc/Guid.h"
#include "Modules/ModuleManager.h"

class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;
class SUnrealAiEditorChatTab;
class SWidget;
class FJsonObject;

class FUnrealAiEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FUnrealAiEditorModule& Get();
	static TSharedPtr<FUnrealAiBackendRegistry> GetBackendRegistry();

	/** Last Agent Chat tab that registered itself (for context menu + drag-drop). */
	static void SetActiveChatSession(TSharedPtr<FUnrealAiChatUiSession> Session);
	static TSharedPtr<FUnrealAiChatUiSession> GetActiveChatSession();
	static void NotifyContextAttachmentsChanged();
	static FSimpleMulticastDelegate& OnContextAttachmentsChanged();

	/** Global: allow optional post-tool editor navigation (focused flag) and non-essential UI (e.g. Content Browser sync). Default false. Persisted in plugin_settings.json under ui.editorFocus. */
	static bool IsEditorFocusEnabled();
	static void SetEditorFocusEnabled(bool bEnabled);
	static void HydrateEditorFocusFromJsonRoot(const TSharedPtr<FJsonObject>& Root);
	static FSimpleMulticastDelegate& OnEditorFocusPolicyChanged();
	/** Global: allow plan execution to use subagent wave policy. Persisted in plugin_settings.json under agent.useSubagents. */
	static bool IsSubagentsEnabled();
	static void SetSubagentsEnabled(bool bEnabled);
	static void HydrateSubagentsFromJsonRoot(const TSharedPtr<FJsonObject>& Root);
	static FSimpleMulticastDelegate& OnSubagentsPolicyChanged();

	/**
	 * Agent code preference: auto | blueprint_first | cpp_first | blueprint_only | cpp_only.
	 * Persisted under plugin_settings.json agent.codeTypePreference.
	 */
	static FString GetAgentCodeTypePreference();
	static void SetAgentCodeTypePreference(const FString& Preference);
	static void HydrateAgentCodeTypePreferenceFromJsonRoot(const TSharedPtr<FJsonObject>& Root);
	static FSimpleMulticastDelegate& OnAgentCodeTypePreferenceChanged();
	/** Default destructive-tool auto-confirm (avoids LLM retry on missing confirm). agent.autoConfirmDestructive in plugin_settings.json */
	static bool IsAutoConfirmDestructiveEnabled();
	static void SetAutoConfirmDestructiveEnabled(bool bEnabled);
	static void HydrateAutoConfirmDestructiveFromJsonRoot(const TSharedPtr<FJsonObject>& Root);
	static FSimpleMulticastDelegate& OnAutoConfirmDestructivePolicyChanged();

	/**
	 * Opens a new Agent Chat dock tab immediately to the right of the tab that contains FromWidget (same tab stack).
	 * @return The new chat widget, or null on failure.
	 */
	static TSharedPtr<SUnrealAiEditorChatTab> OpenNewAgentChatTabBeside(const TSharedPtr<SWidget>& FromWidget);

	/** Opens a new Agent Chat document tab restoring the given persisted thread id (digits-with-hyphens GUID). */
	static void OpenAgentChatTabWithPersistedThread(const FString& ThreadIdDigitsWithHyphens);

	static void RegisterAgentChatTabForPersistence(const TSharedPtr<SUnrealAiEditorChatTab>& Tab);
	static void UnregisterAgentChatTabForPersistence(const SUnrealAiEditorChatTab* Tab);

	static bool ConsumePendingExplicitChatThreadId(FGuid& Out);
	static void SetPendingExplicitChatThreadId(const FGuid& ThreadId);

	/** After a new Agent Chat tab is spawned; continues multi-tab restore on the next frame. */
	static void NotifyAgentChatTabSpawnedForRestoreChain(const TSharedPtr<SUnrealAiEditorChatTab>& Tab);

	/** Open Agent Chat dock tabs, in registration order (for debug UI / highlighting). */
	static void GetOpenAgentChatThreadIds(TArray<FGuid>& OutOrdered);

private:
	void RegisterMenus();
	void RegisterTabs(const TSharedPtr<FUnrealAiBackendRegistry>& Reg);
	void RegisterSettings();
	void RegisterOpenChatOnStartup();
	void RegisterSaveOpenChatsOnExit();
	void UnregisterMenus();
	void UnregisterTabs();
	void UnregisterSettings();
	void UnregisterOpenChatOnStartup();
	void UnregisterSaveOpenChatsOnExit();

	void BeginRestoreOpenChats(const TArray<FGuid>& Ids);
	void SaveOpenAgentChatTabsNow();
	void OnAgentChatTabSpawnedForRestoreChain(const TSharedPtr<SUnrealAiEditorChatTab>& Tab);

	/** Binds the first Agent Chat dock tab once the global tab manager can accept document inserts (startup-safe). */
	void ScheduleDeferredAgentChatDocumentInsert(const TSharedPtr<FUnrealAiBackendRegistry>& Reg);
	void RemoveDeferredAgentChatInsertTicker();
	void CancelDeferredAgentChatInsertsForShutdown();

	bool ConsumePendingExplicitChatThreadId_Impl(FGuid& Out);

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	FDelegateHandle OpenChatOnStartupHandle;
	FDelegateHandle SaveOpenChatsOnExitHandle;
	FTSTicker::FDelegateHandle DeferredAgentChatInsertTickerHandle;
	/** Bumped whenever a new deferred insert is scheduled (previous ticket abandons). */
	int32 DeferredAgentChatInsertTicket = 0;
	TWeakPtr<FUnrealAiChatUiSession> ActiveChatSession;

	TArray<TWeakPtr<SUnrealAiEditorChatTab>> RegisteredAgentChatTabsForPersistence;

	/** When spawning the next chat tab, bind it to this thread id instead of creating a new one. */
	bool bPendingExplicitChatThreadId = false;
	FGuid PendingExplicitChatThreadId;

	/** Remaining threads to open after the first restored tab finishes spawning (left-to-right). */
	TArray<FGuid> PendingRestoreTail;
	bool bAgentChatRestoreChainBusy = false;

	bool bEditorFocusEnabled = false;
	bool bSubagentsEnabled = true;
	FString AgentCodeTypePreference = TEXT("auto");
	bool bAutoConfirmDestructive = true;
};
