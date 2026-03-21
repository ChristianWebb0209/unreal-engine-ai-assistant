#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FUnrealAiBackendRegistry;

class FUnrealAiEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FUnrealAiEditorModule& Get();
	static TSharedPtr<FUnrealAiBackendRegistry> GetBackendRegistry();

private:
	void RegisterMenus();
	void RegisterTabs();
	void RegisterSettings();
	void UnregisterMenus();
	void UnregisterTabs();
	void UnregisterSettings();

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
};
