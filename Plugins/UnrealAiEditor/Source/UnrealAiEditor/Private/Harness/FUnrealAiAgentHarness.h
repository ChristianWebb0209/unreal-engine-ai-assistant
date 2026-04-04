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

class FUnrealAiPlanExecutor;

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
	virtual void FailInProgressTurnForScenarioIdleAbort() override;
	virtual bool IsTurnInProgress() const override;
	virtual bool HasActiveLlmTransportRequest() const override;
	virtual bool ShouldSuppressIdleAbort() const override;
	virtual void NotifyPlanExecutorStarted(TSharedPtr<FUnrealAiPlanExecutor> Exec) override;
	virtual void NotifyPlanExecutorEnded() override;
	virtual bool IsPlanPipelineActive() const override;
	virtual void SetHeadedScenarioStrictToolBudgets(bool bEnable) override;
	virtual void SetHeadedScenarioSyncRun(bool bEnable) override;

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
	TWeakPtr<FUnrealAiPlanExecutor> WeakActivePlanExecutor;
	bool bHeadedScenarioStrictToolBudgets = false;
	bool bHeadedScenarioSyncRun = false;
};
