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

	virtual void SetEditorSnapshot(const FEditorContextSnapshot& Snapshot) = 0;
	virtual void ClearEditorSnapshot() = 0;

	/** Capture current editor selection / active asset (Phase C). */
	virtual void RefreshEditorSnapshotFromEngine() = 0;

	virtual FAgentContextBuildResult BuildContextWindow(const FAgentContextBuildOptions& Options) = 0;

	virtual void SaveNow(const FString& ProjectId, const FString& ThreadId) = 0;

	/** Persist every loaded session (e.g. editor shutdown). */
	virtual void FlushAllSessionsToDisk() = 0;

	virtual const FAgentContextState* GetState(const FString& ProjectId, const FString& ThreadId) const = 0;
};
