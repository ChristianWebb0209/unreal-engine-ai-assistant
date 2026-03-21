#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Harness/UnrealAiOrchestrateDag.h"

class IUnrealAiAgentHarness;
class IAgentContextService;
class IAgentRunSink;

class FUnrealAiOrchestrateExecutor : public TSharedFromThis<FUnrealAiOrchestrateExecutor>
{
public:
	static TSharedRef<FUnrealAiOrchestrateExecutor> Start(
		IUnrealAiAgentHarness* Harness,
		IAgentContextService* ContextService,
		const FUnrealAiAgentTurnRequest& ParentRequest,
		TSharedPtr<IAgentRunSink> ParentSink);

	void Cancel();
	bool IsRunning() const { return bRunning; }

private:
	void BeginPlannerTurn();
	void OnPlannerFinished(bool bSuccess, const FString& ErrorText, const FString& PlannerText);
	void BeginNextReadyNode();
	void OnNodeFinished(const FString& NodeId, bool bSuccess, const FString& ErrorText, const FString& AssistantText);
	void Finish(bool bSuccess, const FString& ErrorText);

	IUnrealAiAgentHarness* Harness = nullptr;
	IAgentContextService* ContextService = nullptr;
	FUnrealAiAgentTurnRequest ParentRequest;
	TSharedPtr<IAgentRunSink> ParentSink;
	bool bRunning = false;
	bool bCancelled = false;
	int32 NodeCursor = 0;
	TArray<FString> ReadyNodeIds;
	FUnrealAiOrchestrateDag Dag;
	FUnrealAiRunIds ParentIds;
};

