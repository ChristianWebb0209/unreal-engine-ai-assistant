#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;
class SWidget;

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

	/** Opens a new Agent Chat dock tab immediately to the right of the tab that contains FromWidget (same tab stack). */
	static void OpenNewAgentChatTabBeside(const TSharedPtr<SWidget>& FromWidget);

private:
	void RegisterMenus();
	void RegisterTabs(const TSharedPtr<FUnrealAiBackendRegistry>& Reg);
	void RegisterSettings();
	void RegisterOpenChatOnStartup();
	void UnregisterMenus();
	void UnregisterTabs();
	void UnregisterSettings();
	void UnregisterOpenChatOnStartup();

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	FDelegateHandle OpenChatOnStartupHandle;
	TWeakPtr<FUnrealAiChatUiSession> ActiveChatSession;
};
