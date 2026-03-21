#pragma once

#include "CoreMinimal.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Harness/FUnrealAiAgentHarness.h"

class IUnrealAiPersistence;
class IUnrealAiChatService;
class IUnrealAiModelConnector;
class IAgentContextService;
class IToolExecutionHost;
class FUnrealAiAgentHarness;
class FUnrealAiModelProfileRegistry;
class FUnrealAiToolCatalog;
class FUnrealAiModelPricingCatalog;
class FUnrealAiUsageTracker;

class FUnrealAiBackendRegistry : public TSharedFromThis<FUnrealAiBackendRegistry>
{
public:
	FUnrealAiBackendRegistry();
	virtual ~FUnrealAiBackendRegistry();

	IUnrealAiPersistence* GetPersistence() const { return Persistence.Get(); }
	IUnrealAiChatService* GetChatService() const { return ChatService.Get(); }
	IUnrealAiModelConnector* GetModelConnector() const { return ModelConnector.Get(); }
	IAgentContextService* GetContextService() const { return ContextService.Get(); }
	IToolExecutionHost* GetToolExecutionHost() const { return ToolExecutionHost.Get(); }
	IUnrealAiAgentHarness* GetAgentHarness() const { return AgentHarness.Get(); }
	FUnrealAiModelProfileRegistry* GetModelProfileRegistry() const { return ModelProfiles.Get(); }
	FUnrealAiToolCatalog* GetToolCatalog() const { return ToolCatalog.Get(); }
	FUnrealAiModelPricingCatalog* GetPricingCatalog() const { return PricingCatalog.Get(); }
	FUnrealAiUsageTracker* GetUsageTracker() const { return UsageTracker.Get(); }

	/** Reload `plugin_settings.json` from disk, refresh stub vs HTTP transport, recreate agent harness. Call after API keys / providers change. */
	void ReloadLlmConfiguration();

private:
	void RefreshActiveLlmTransport();
	void RefreshModelConnector();
	void RebuildAgentHarness();
	TUniquePtr<IUnrealAiPersistence> Persistence;
	TUniquePtr<IAgentContextService> ContextService;
	TUniquePtr<IUnrealAiChatService> ChatService;
	TUniquePtr<IUnrealAiModelConnector> ModelConnector;
	TUniquePtr<IToolExecutionHost> ToolExecutionHost;

	TUniquePtr<FUnrealAiModelProfileRegistry> ModelProfiles;
	TUniquePtr<FUnrealAiModelPricingCatalog> PricingCatalog;
	TUniquePtr<FUnrealAiUsageTracker> UsageTracker;
	TUniquePtr<FUnrealAiToolCatalog> ToolCatalog;
	TSharedPtr<class FOpenAiCompatibleHttpTransport> HttpTransport;
	TSharedPtr<class FUnrealAiLlmTransportStub> StubTransport;
	TSharedPtr<class ILlmTransport> ActiveLlmTransport;
	TUniquePtr<FUnrealAiAgentHarness> AgentHarness;
};
