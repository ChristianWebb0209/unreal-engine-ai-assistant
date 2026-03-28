#pragma once

#include "CoreMinimal.h"
#include "Harness/IUnrealAiAgentHarness.h"

class IUnrealAiPersistence;
class IAgentContextService;
class FUnrealAiModelProfileRegistry;
class FUnrealAiToolCatalog;
class ILlmTransport;
class IToolExecutionHost;
class FUnrealAiUsageTracker;
class IUnrealAiMemoryService;

namespace UnrealAiAgentHarnessPriv
{
	struct FAgentTurnRunner;
}

/** Plans context, conversation store, tool catalog, transport, and tool host. */
class FUnrealAiAgentHarness final : public IUnrealAiAgentHarness
{
public:
	FUnrealAiAgentHarness(
		IUnrealAiPersistence* InPersistence,
		IAgentContextService* InContext,
		FUnrealAiModelProfileRegistry* InProfiles,
		FUnrealAiToolCatalog* InCatalog,
		TSharedPtr<ILlmTransport> InTransport,
		IToolExecutionHost* InToolHost,
		FUnrealAiUsageTracker* InUsageTracker,
		IUnrealAiMemoryService* InMemoryService);
	virtual ~FUnrealAiAgentHarness() override;

	virtual void RunTurn(const FUnrealAiAgentTurnRequest& Request, TSharedPtr<IAgentRunSink> Sink) override;
	virtual void CancelTurn() override;
	virtual bool IsTurnInProgress() const override;

private:
	IUnrealAiPersistence* Persistence = nullptr;
	IAgentContextService* Context = nullptr;
	FUnrealAiModelProfileRegistry* Profiles = nullptr;
	FUnrealAiToolCatalog* Catalog = nullptr;
	TSharedPtr<ILlmTransport> Transport;
	IToolExecutionHost* ToolHost = nullptr;
	FUnrealAiUsageTracker* UsageTracker = nullptr;
	IUnrealAiMemoryService* MemoryService = nullptr;

	TSharedPtr<UnrealAiAgentHarnessPriv::FAgentTurnRunner> ActiveRunner;
};
