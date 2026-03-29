#pragma once

#include "Context/AgentContextTypes.h"

class IAgentContextService
{
public:
	virtual ~IAgentContextService() = default;

	virtual void LoadOrCreate(const FString& ProjectId, const FString& ThreadId) = 0;
	virtual void ClearSession(const FString& ProjectId, const FString& ThreadId) = 0;

	virtual void AddAttachment(const FContextAttachment& Attachment) = 0;
	virtual void RemoveAttachment(int32 Index) = 0;
	virtual void ClearAttachments() = 0;

	virtual void RecordToolResult(FName ToolName, const FString& Result, const FContextRecordPolicy& Policy) = 0;

	/** Persist structured todo plan JSON (replaces prior plan + clears step checkboxes). */
	virtual void SetActiveTodoPlan(const FString& PlanJson) = 0;
	virtual void SetTodoStepDone(int32 StepIndex, bool bDone) = 0;
	/** Persist plan DAG JSON and reset node status maps. */
	virtual void SetActivePlanDag(const FString& DagJson) = 0;
	/** Persist plan DAG on an explicit parent session (project/thread), independent of active child thread. */
	virtual void SetActivePlanDagForThread(const FString& ProjectId, const FString& ThreadId, const FString& DagJson) = 0;
	/**
	 * Replace plan DAG JSON and scrub status/summary entries: drop keys not in the new graph, then clear any id in
	 * FreshNodeIds so replanned nodes start pending (preserves completed successes for kept ids).
	 */
	virtual void ReplaceActivePlanDagWithFreshNodeReset(const FString& DagJson, const TSet<FString>& FreshNodeIds) = 0;
	virtual void ReplaceActivePlanDagWithFreshNodeResetForThread(
		const FString& ProjectId,
		const FString& ThreadId,
		const FString& DagJson,
		const TSet<FString>& FreshNodeIds) = 0;
	/** Update per-node plan status and optional summary. */
	virtual void SetPlanNodeStatus(const FString& NodeId, const FString& Status, const FString& Summary = FString()) = 0;
	virtual void SetPlanNodeStatusForThread(
		const FString& ProjectId,
		const FString& ThreadId,
		const FString& NodeId,
		const FString& Status,
		const FString& Summary = FString()) = 0;
	/** Removes in-flight "running" markers so execution can resume after a crash or lost harness callback. */
	virtual void ClearPlanStaleRunningMarkers(const FString& ProjectId, const FString& ThreadId) = 0;
	/** Clear active plan DAG and any node execution status. */
	virtual void ClearActivePlanDag() = 0;

	virtual void SetEditorSnapshot(const FEditorContextSnapshot& Snapshot) = 0;
	virtual void ClearEditorSnapshot() = 0;

	/** Capture current editor selection / active asset (Phase C). */
	virtual void RefreshEditorSnapshotFromEngine() = 0;
	virtual void StartRetrievalPrefetch(const FString& TurnKey, const FString& UserMessageForComplexity) = 0;
	virtual void CancelRetrievalPrefetchForThread(const FString& ProjectId, const FString& ThreadId) = 0;

	/** Assembles ranked context for the LLM. Must run on the game thread (editor snapshot, attachment resolution, retrieval hooks). */
	virtual FAgentContextBuildResult BuildContextWindow(const FAgentContextBuildOptions& Options) = 0;

	virtual void SaveNow(const FString& ProjectId, const FString& ThreadId) = 0;

	/** Persist every loaded session (e.g. editor shutdown). */
	virtual void FlushAllSessionsToDisk() = 0;

	/** Drop all in-memory thread sessions without writing (e.g. after deleting chat files on disk). */
	virtual void WipeAllSessionsInMemory() = 0;

	virtual const FAgentContextState* GetState(const FString& ProjectId, const FString& ThreadId) const = 0;
};
