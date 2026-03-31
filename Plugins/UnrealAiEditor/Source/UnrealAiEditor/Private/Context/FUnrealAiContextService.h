#pragma once

#include "Context/IAgentContextService.h"
#include "Context/UnrealAiContextCandidates.h"
#include "Containers/Ticker.h"

class IUnrealAiPersistence;
class IUnrealAiMemoryService;
class IUnrealAiRetrievalService;

class FUnrealAiContextService final : public IAgentContextService
{
public:
	FUnrealAiContextService(
		IUnrealAiPersistence* InPersistence,
		IUnrealAiMemoryService* InMemoryService,
		IUnrealAiRetrievalService* InRetrievalService = nullptr);

	virtual void LoadOrCreate(const FString& ProjectId, const FString& ThreadId) override;
	virtual void ClearSession(const FString& ProjectId, const FString& ThreadId) override;

	virtual void AddAttachment(const FContextAttachment& Attachment) override;
	virtual void RemoveAttachment(int32 Index) override;
	virtual void ClearAttachments() override;

	virtual void RecordToolResult(FName ToolName, const FString& Result, const FContextRecordPolicy& Policy) override;

	virtual void SetActiveTodoPlan(const FString& PlanJson) override;
	virtual void SetTodoStepDone(int32 StepIndex, bool bDone) override;
	virtual void SetActivePlanDag(const FString& DagJson) override;
	virtual void SetActivePlanDagForThread(const FString& ProjectId, const FString& ThreadId, const FString& DagJson) override;
	virtual void ReplaceActivePlanDagWithFreshNodeReset(const FString& DagJson, const TSet<FString>& FreshNodeIds) override;
	virtual void ReplaceActivePlanDagWithFreshNodeResetForThread(
		const FString& ProjectId,
		const FString& ThreadId,
		const FString& DagJson,
		const TSet<FString>& FreshNodeIds) override;
	virtual void SetPlanNodeStatus(const FString& NodeId, const FString& Status, const FString& Summary = FString()) override;
	virtual void SetPlanNodeStatusForThread(
		const FString& ProjectId,
		const FString& ThreadId,
		const FString& NodeId,
		const FString& Status,
		const FString& Summary = FString()) override;
	virtual void ClearPlanStaleRunningMarkers(const FString& ProjectId, const FString& ThreadId) override;
	virtual void ClearActivePlanDag() override;

	virtual void SetEditorSnapshot(const FEditorContextSnapshot& Snapshot) override;
	virtual void ClearEditorSnapshot() override;
	virtual void RefreshEditorSnapshotFromEngine() override;
	virtual void StartRetrievalPrefetch(const FString& TurnKey, const FString& UserMessageForComplexity) override;
	virtual void CancelRetrievalPrefetchForThread(const FString& ProjectId, const FString& ThreadId) override;

	virtual FAgentContextBuildResult BuildContextWindow(const FAgentContextBuildOptions& Options) override;

	virtual void SaveNow(const FString& ProjectId, const FString& ThreadId) override;

	virtual void FlushAllSessionsToDisk() override;
	virtual void WipeAllSessionsInMemory() override;

	virtual const FAgentContextState* GetState(const FString& ProjectId, const FString& ThreadId) const override;

private:
	struct FRetrievalEntityUtilityCounters
	{
		float UtilityScore = 0.0f;
		int32 RetrievalHitCount = 0;
		int32 KeptCount = 0;
		int32 BudgetDropCount = 0;
		int32 ActionRefCount = 0;
		int32 StaleTurns = 0;
	};

	struct FProjectRetrievalState
	{
		TMap<FString, FRetrievalEntityUtilityCounters> CountersByEntity;
		TSet<FString> HeadEntitySet;
		TMap<FString, TMap<FString, int32>> EdgeWeightsByEntity;
		FDateTime UpdatedUtc = FDateTime::MinValue();
	};

	static FString SessionKey(const FString& ProjectId, const FString& ThreadId);
	FAgentContextState* FindOrAddState(const FString& ProjectId, const FString& ThreadId);
	const FAgentContextState* FindState(const FString& ProjectId, const FString& ThreadId) const;
	void ScheduleSave(const FString& ProjectId, const FString& ThreadId);
	void FlushSave(const FString& ProjectId, const FString& ThreadId);
	void FlushSaveBySessionKey(const FString& Key);
	bool ValidateAttachment(const FContextAttachment& Attachment, FString* OutWarning) const;
	void UpdateRetrievalUtilityCounters(
		const FString& ProjectId,
		const FString& SessionId,
		const TArray<UnrealAiContextCandidates::FContextCandidateEnvelope>& Packed,
		const TArray<UnrealAiContextCandidates::FContextCandidateEnvelope>& Dropped);
	void RegisterActionReferencesFromToolResult(
		const FString& ProjectId,
		const FString& SessionId,
		const FString& ToolResultText);
	void RegisterPackedEntityState(
		const FString& SessionId,
		const TArray<UnrealAiContextCandidates::FContextCandidateEnvelope>& Packed);
	void BuildRetrievalUtilityOptions(
		const FString& ProjectId,
		const FString& SessionId,
		FAgentContextBuildOptions& InOutOptions) const;
	void RefreshHeadSetForProject(const FString& ProjectId);
	void SaveProjectRetrievalState(const FString& ProjectId);
	void LoadProjectRetrievalState(const FString& ProjectId);
	FString GetProjectRetrievalStatePath(const FString& ProjectId) const;
	static void ExtractCanonicalActionRefs(const FString& Text, TSet<FString>& OutRefs);
	static bool IsBudgetOrCapDropReason(const FString& DropReason);

	IUnrealAiPersistence* Persistence = nullptr;
	IUnrealAiMemoryService* MemoryService = nullptr;
	IUnrealAiRetrievalService* RetrievalService = nullptr;
	TMap<FString, FAgentContextState> Sessions;
	/** Project-scoped dynamic tree cache shared across threads. */
	TMap<FString, FProjectTreeSummary> ProjectTreeByProjectId;
	/** In-memory (phase 0) per-thread utility counters keyed by derived retrieval entity id. */
	TMap<FString, TMap<FString, FRetrievalEntityUtilityCounters>> RetrievalUtilityBySession;
	/** Packed entity metadata from last build per session (for action-ref attribution). */
	TMap<FString, TMap<FString, TSet<FString>>> LastPackedCanonicalRefsBySessionByEntity;
	/** Durable project-global retrieval utility/head state. */
	TMap<FString, FProjectRetrievalState> RetrievalStateByProject;
	FString ActiveProjectId;
	FString ActiveThreadId;
	TSet<FString> PendingSaveKeys;
	FTSTicker::FDelegateHandle SaveTickerHandle;
};
