#pragma once

#include "Context/IAgentContextService.h"
#include "Containers/Ticker.h"

class IUnrealAiPersistence;

class FUnrealAiContextService final : public IAgentContextService
{
public:
	explicit FUnrealAiContextService(IUnrealAiPersistence* InPersistence);

	virtual void LoadOrCreate(const FString& ProjectId, const FString& ThreadId) override;
	virtual void ClearSession(const FString& ProjectId, const FString& ThreadId) override;

	virtual void AddAttachment(const FContextAttachment& Attachment) override;
	virtual void RemoveAttachment(int32 Index) override;
	virtual void ClearAttachments() override;

	virtual void RecordToolResult(FName ToolName, const FString& Result, const FContextRecordPolicy& Policy) override;

	virtual void SetActiveTodoPlan(const FString& PlanJson) override;
	virtual void SetTodoStepDone(int32 StepIndex, bool bDone) override;
	virtual void SetActiveOrchestrateDag(const FString& DagJson) override;
	virtual void SetOrchestrateNodeStatus(const FString& NodeId, const FString& Status, const FString& Summary = FString()) override;
	virtual void ClearActiveOrchestrateDag() override;

	virtual void SetEditorSnapshot(const FEditorContextSnapshot& Snapshot) override;
	virtual void ClearEditorSnapshot() override;
	virtual void RefreshEditorSnapshotFromEngine() override;

	virtual FAgentContextBuildResult BuildContextWindow(const FAgentContextBuildOptions& Options) override;

	virtual void SaveNow(const FString& ProjectId, const FString& ThreadId) override;

	virtual void FlushAllSessionsToDisk() override;

	virtual const FAgentContextState* GetState(const FString& ProjectId, const FString& ThreadId) const override;

private:
	static FString SessionKey(const FString& ProjectId, const FString& ThreadId);
	FAgentContextState* FindOrAddState(const FString& ProjectId, const FString& ThreadId);
	const FAgentContextState* FindState(const FString& ProjectId, const FString& ThreadId) const;
	void ScheduleSave(const FString& ProjectId, const FString& ThreadId);
	void FlushSave(const FString& ProjectId, const FString& ThreadId);
	void FlushSaveBySessionKey(const FString& Key);
	bool ValidateAttachment(const FContextAttachment& Attachment, FString* OutWarning) const;

	IUnrealAiPersistence* Persistence = nullptr;
	TMap<FString, FAgentContextState> Sessions;
	FString ActiveProjectId;
	FString ActiveThreadId;
	TSet<FString> PendingSaveKeys;
	FTSTicker::FDelegateHandle SaveTickerHandle;
};
