#include "Context/FUnrealAiContextService.h"

#include "Backend/IUnrealAiPersistence.h"
#include "Context/AgentContextFormat.h"
#include "Context/AgentContextJson.h"
#include "Context/UnrealAiContextCandidates.h"
#include "Context/UnrealAiContextDecisionLogger.h"
#include "Context/UnrealAiEditorContextQueries.h"
#include "Context/UnrealAiProjectTreeSampler.h"
#include "Context/UnrealAiStartupOpsStatus.h"
#include "Context/UnrealAiRecentUiRanking.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Observability/UnrealAiBackgroundOpsLog.h"
#include "Retrieval/IUnrealAiRetrievalService.h"
#include "Retrieval/UnrealAiRetrievalObservability.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "Selection.h"
#endif

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Misc/App.h"
#include "Context/UnrealAiActiveTodoSummary.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/UnrealAiRecentUiTracker.h"
#include "Planning/UnrealAiPlanDag.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

namespace UnrealAiCtxPlanChildPriv
{
	static const TCHAR* GPlanThreadMarker = TEXT("_plan_");

	/** Split `<parent>_plan_<nodeId>` from the rightmost `_plan_` so parent thread ids may contain the substring. */
	static bool TrySplitPlanChildThreadId(const FString& ThreadId, FString& OutParentThreadId, FString& OutNodeIdSuffix)
	{
		const int32 LastIdx = ThreadId.Find(GPlanThreadMarker, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (LastIdx == INDEX_NONE)
		{
			return false;
		}
		OutParentThreadId = ThreadId.Left(LastIdx);
		OutNodeIdSuffix = ThreadId.Mid(LastIdx + FCString::Strlen(GPlanThreadMarker));
		return !OutParentThreadId.IsEmpty() && !OutNodeIdSuffix.IsEmpty();
	}
}

namespace UnrealAiCtxTodoPriv
{
	static int32 CountTodoStepsInJson(const FString& PlanJson)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PlanJson);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return 0;
		}
		const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
		if (!Root->TryGetArrayField(TEXT("steps"), Steps) || !Steps)
		{
			return 0;
		}
		return Steps->Num();
	}
}

namespace UnrealAiCtxMutationPriv
{
	static bool IsLikelySceneOrContentMutationTool(const FString& ToolName)
	{
		const FString T = ToolName.ToLower();
		if (T.StartsWith(TEXT("actor_"))
			|| T.StartsWith(TEXT("viewport_camera_"))
			|| T.StartsWith(TEXT("asset_"))
			|| T.StartsWith(TEXT("blueprint_apply_"))
			|| T == TEXT("blueprint_graph_patch")
			|| T == TEXT("blueprint_compile")
			|| T == TEXT("blueprint_set_component_default")
			|| T.StartsWith(TEXT("content_browser_sync_")))
		{
			return true;
		}
		return false;
	}

	static bool ShouldRequestMutationTriggeredRebuild(
		TMap<FString, FDateTime>& LastRebuildByProject,
		const FString& ProjectId,
		const FDateTime NowUtc)
	{
		constexpr int32 CooldownSeconds = 20;
		FDateTime& Last = LastRebuildByProject.FindOrAdd(ProjectId);
		if (Last != FDateTime::MinValue())
		{
			const FTimespan Delta = NowUtc - Last;
			if (Delta.GetTotalSeconds() < static_cast<double>(CooldownSeconds))
			{
				return false;
			}
		}
		Last = NowUtc;
		return true;
	}
}

FUnrealAiContextService::FUnrealAiContextService(
	IUnrealAiPersistence* InPersistence,
	IUnrealAiMemoryService* InMemoryService,
	IUnrealAiRetrievalService* InRetrievalService)
	: Persistence(InPersistence)
	, MemoryService(InMemoryService)
	, RetrievalService(InRetrievalService)
{
}

FString FUnrealAiContextService::SessionKey(const FString& ProjectId, const FString& ThreadId)
{
	return ProjectId + TEXT("|") + ThreadId;
}

FAgentContextState* FUnrealAiContextService::FindOrAddState(const FString& ProjectId, const FString& ThreadId)
{
	return &Sessions.FindOrAdd(SessionKey(ProjectId, ThreadId));
}

const FAgentContextState* FUnrealAiContextService::FindState(const FString& ProjectId, const FString& ThreadId) const
{
	return Sessions.Find(SessionKey(ProjectId, ThreadId));
}

void FUnrealAiContextService::LoadOrCreate(const FString& ProjectId, const FString& ThreadId)
{
	ActiveProjectId = ProjectId;
	ActiveThreadId = ThreadId;
	LoadProjectRetrievalState(ProjectId);

	const FString Key = SessionKey(ProjectId, ThreadId);
	if (Sessions.Contains(Key))
	{
		return;
	}

	FString Json;
	if (Persistence && Persistence->LoadThreadContextJson(ProjectId, ThreadId, Json))
	{
		TArray<FString> Warnings;
		FAgentContextState Loaded;
		if (UnrealAiAgentContextJson::JsonToState(Json, Loaded, Warnings))
		{
			if (Loaded.ProjectTreeSummary.UpdatedUtc != FDateTime::MinValue())
			{
				FProjectTreeSummary& ProjectSummary = ProjectTreeByProjectId.FindOrAdd(ProjectId);
				if (ProjectSummary.UpdatedUtc == FDateTime::MinValue() || Loaded.ProjectTreeSummary.UpdatedUtc > ProjectSummary.UpdatedUtc)
				{
					ProjectSummary = Loaded.ProjectTreeSummary;
				}
			}
			Sessions.Add(Key, MoveTemp(Loaded));
			return;
		}
	}

	Sessions.Add(Key, FAgentContextState());
}

void FUnrealAiContextService::ClearSession(const FString& ProjectId, const FString& ThreadId)
{
	if (RetrievalService)
	{
		RetrievalService->CancelPrefetchForThread(ProjectId, ThreadId);
	}
	const FString Key = SessionKey(ProjectId, ThreadId);
	Sessions.Remove(Key);
	RetrievalUtilityBySession.Remove(Key);
	LastPackedCanonicalRefsBySessionByEntity.Remove(Key);
	if (ActiveProjectId == ProjectId && ActiveThreadId == ThreadId)
	{
		ActiveProjectId.Reset();
		ActiveThreadId.Reset();
	}
}

void FUnrealAiContextService::AddAttachment(const FContextAttachment& Attachment)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FString Warn;
	if (!ValidateAttachment(Attachment, &Warn) && !Warn.IsEmpty())
	{
		UE_LOG(LogCore, Warning, TEXT("Context attachment: %s"), *Warn);
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->Attachments.Add(Attachment);
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

bool FUnrealAiContextService::ValidateAttachment(const FContextAttachment& Attachment, FString* OutWarning) const
{
#if WITH_EDITOR
	if (Attachment.Type == EContextAttachmentType::AssetPath && !Attachment.Payload.IsEmpty())
	{
		FAssetRegistryModule& Reg =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FSoftObjectPath Path(Attachment.Payload);
		const FAssetData AD = Reg.Get().GetAssetByObjectPath(Path);
		if (!AD.IsValid())
		{
			if (OutWarning)
			{
				*OutWarning = FString::Printf(TEXT("Asset not found in registry: %s"), *Attachment.Payload);
			}
			return false;
		}
	}
	if (Attachment.Type == EContextAttachmentType::ContentFolder && Attachment.Payload.IsEmpty())
	{
		if (OutWarning)
		{
			*OutWarning = TEXT("Empty folder path.");
		}
		return false;
	}
#endif
	return true;
}

void FUnrealAiContextService::RemoveAttachment(int32 Index)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	if (S->Attachments.IsValidIndex(Index))
	{
		S->Attachments.RemoveAt(Index);
		ScheduleSave(ActiveProjectId, ActiveThreadId);
	}
}

void FUnrealAiContextService::ClearAttachments()
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->Attachments.Reset();
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::RecordToolResult(
	FName ToolName,
	const FString& Result,
	const FContextRecordPolicy& Policy)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	FToolContextEntry E;
	E.ToolName = ToolName.ToString();
	E.TruncatedResult = Result;
	if (E.TruncatedResult.Len() > Policy.MaxStoredCharsPerResult)
	{
		E.TruncatedResult = E.TruncatedResult.Left(Policy.MaxStoredCharsPerResult);
	}
	E.Timestamp = FDateTime::UtcNow();
	S->ToolResults.Add(MoveTemp(E));
	RegisterActionReferencesFromToolResult(ActiveProjectId, SessionKey(ActiveProjectId, ActiveThreadId), Result);
	ScheduleSave(ActiveProjectId, ActiveThreadId);
	const FString Tool = ToolName.ToString();
	if (UnrealAiCtxMutationPriv::IsLikelySceneOrContentMutationTool(Tool))
	{
		// Keep context snapshot fresh after scene/content mutations so follow-up turns can ground to latest state.
		RefreshEditorSnapshotFromEngine();
		if (RetrievalService
			&& UnrealAiCtxMutationPriv::ShouldRequestMutationTriggeredRebuild(
				LastMutationTriggeredRetrievalRebuildUtcByProject,
				ActiveProjectId,
				FDateTime::UtcNow()))
		{
			RetrievalService->RequestRebuild(ActiveProjectId);
		}
	}
}

void FUnrealAiContextService::SetActiveTodoPlan(const FString& PlanJson)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->ActiveTodoPlanJson = PlanJson;
	const int32 N = UnrealAiCtxTodoPriv::CountTodoStepsInJson(PlanJson);
	S->TodoStepsDone.Init(false, N);
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::SetTodoStepDone(int32 StepIndex, bool bDone)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	if (!S->TodoStepsDone.IsValidIndex(StepIndex))
	{
		return;
	}
	S->TodoStepsDone[StepIndex] = bDone;
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::SetActivePlanDag(const FString& DagJson)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	SetActivePlanDagForThread(ActiveProjectId, ActiveThreadId, DagJson);
}

void FUnrealAiContextService::SetActivePlanDagForThread(const FString& ProjectId, const FString& ThreadId, const FString& DagJson)
{
	if (ProjectId.IsEmpty() || ThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ProjectId, ThreadId);
	S->ActivePlanDagJson = DagJson;
	S->PlanNodeStatusById.Reset();
	S->PlanNodeSummaryById.Reset();
	ScheduleSave(ProjectId, ThreadId);
}

void FUnrealAiContextService::ReplaceActivePlanDagWithFreshNodeReset(const FString& DagJson, const TSet<FString>& FreshNodeIds)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	ReplaceActivePlanDagWithFreshNodeResetForThread(ActiveProjectId, ActiveThreadId, DagJson, FreshNodeIds);
}

void FUnrealAiContextService::ReplaceActivePlanDagWithFreshNodeResetForThread(
	const FString& ProjectId,
	const FString& ThreadId,
	const FString& DagJson,
	const TSet<FString>& FreshNodeIds)
{
	if (ProjectId.IsEmpty() || ThreadId.IsEmpty())
	{
		return;
	}
	FUnrealAiPlanDag Parsed;
	FString Err;
	if (!UnrealAiPlanDag::ParseDagJson(DagJson, Parsed, Err))
	{
		return;
	}
	TSet<FString> AllIds;
	for (const FUnrealAiDagNode& N : Parsed.Nodes)
	{
		AllIds.Add(N.Id);
	}
	FAgentContextState* S = FindOrAddState(ProjectId, ThreadId);
	S->ActivePlanDagJson = DagJson;
	for (auto It = S->PlanNodeStatusById.CreateIterator(); It; ++It)
	{
		if (!AllIds.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = S->PlanNodeSummaryById.CreateIterator(); It; ++It)
	{
		if (!AllIds.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	for (const FString& Id : FreshNodeIds)
	{
		if (AllIds.Contains(Id))
		{
			S->PlanNodeStatusById.Remove(Id);
			S->PlanNodeSummaryById.Remove(Id);
		}
	}
	ScheduleSave(ProjectId, ThreadId);
}

void FUnrealAiContextService::SetPlanNodeStatus(const FString& NodeId, const FString& Status, const FString& Summary)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty() || NodeId.IsEmpty())
	{
		return;
	}
	SetPlanNodeStatusForThread(ActiveProjectId, ActiveThreadId, NodeId, Status, Summary);
}

void FUnrealAiContextService::SetPlanNodeStatusForThread(
	const FString& ProjectId,
	const FString& ThreadId,
	const FString& NodeId,
	const FString& Status,
	const FString& Summary)
{
	if (ProjectId.IsEmpty() || ThreadId.IsEmpty() || NodeId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ProjectId, ThreadId);
	S->PlanNodeStatusById.Add(NodeId, Status);
	if (!Summary.IsEmpty())
	{
		S->PlanNodeSummaryById.Add(NodeId, Summary);
	}
	ScheduleSave(ProjectId, ThreadId);
}

void FUnrealAiContextService::ClearPlanStaleRunningMarkers(const FString& ProjectId, const FString& ThreadId)
{
	if (ProjectId.IsEmpty() || ThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ProjectId, ThreadId);
	TArray<FString> RemoveIds;
	for (const TPair<FString, FString>& Pair : S->PlanNodeStatusById)
	{
		if (Pair.Value.Equals(TEXT("running"), ESearchCase::IgnoreCase))
		{
			RemoveIds.Add(Pair.Key);
		}
	}
	if (RemoveIds.Num() == 0)
	{
		return;
	}
	for (const FString& Id : RemoveIds)
	{
		S->PlanNodeStatusById.Remove(Id);
		S->PlanNodeSummaryById.Remove(Id);
	}
	ScheduleSave(ProjectId, ThreadId);
}

void FUnrealAiContextService::ClearActivePlanDag()
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->ActivePlanDagJson.Empty();
	S->PlanNodeStatusById.Reset();
	S->PlanNodeSummaryById.Reset();
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::SetEditorSnapshot(const FEditorContextSnapshot& Snapshot)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->EditorSnapshot = Snapshot;
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::ClearEditorSnapshot()
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->EditorSnapshot.Reset();
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::RefreshEditorSnapshotFromEngine()
{
#if WITH_EDITOR
	FEditorContextSnapshot Snap;
	Snap.bValid = true;
	FUnrealAiRecentUiTracker::SetActiveThreadId(ActiveThreadId);
	FUnrealAiRecentUiTracker::MarkCurrentFocusNow();
	if (GEditor)
	{
		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (SelectedActors)
		{
			TArray<FString> Names;
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				if (AActor* A = Cast<AActor>(*It))
				{
					Names.Add(A->GetActorLabel());
				}
			}
			Snap.SelectedActorsSummary = FString::Join(Names, TEXT(", "));
		}
	}
	UnrealAiEditorContextQueries::PopulateContentBrowserAndSelection(Snap);
	UnrealAiEditorContextQueries::PopulateOpenEditorAssets(Snap);
	TArray<FRecentUiEntry> GlobalHistory;
	TArray<FRecentUiEntry> ThreadOverlay;
	FUnrealAiRecentUiTracker::GetProjectGlobalHistory(GlobalHistory);
	FUnrealAiRecentUiTracker::GetThreadOverlay(ActiveThreadId, ThreadOverlay);
	if (ThreadOverlay.Num() == 0 && !ActiveProjectId.IsEmpty() && !ActiveThreadId.IsEmpty())
	{
		if (const FAgentContextState* Existing = FindState(ActiveProjectId, ActiveThreadId))
		{
			ThreadOverlay = Existing->ThreadRecentUiOverlay;
		}
	}
	UnrealAiRecentUiRanking::MergeAndRank(GlobalHistory, ThreadOverlay, 18, Snap.RecentUiEntries);
	for (const FRecentUiEntry& E : Snap.RecentUiEntries)
	{
		if (E.bCurrentlyActive)
		{
			Snap.ActiveUiEntryId = E.StableId;
			break;
		}
	}
	if (!ActiveProjectId.IsEmpty() && !ActiveThreadId.IsEmpty())
	{
		FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
		S->ThreadRecentUiOverlay = ThreadOverlay;
	}
	SetEditorSnapshot(Snap);
#endif
}

void FUnrealAiContextService::StartRetrievalPrefetch(const FString& TurnKey, const FString& UserMessageForComplexity)
{
	if (TurnKey.IsEmpty() || UserMessageForComplexity.TrimStartAndEnd().IsEmpty())
	{
		return;
	}
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	if (!RetrievalService || !RetrievalService->IsEnabledForProject(ActiveProjectId))
	{
		return;
	}

	FUnrealAiRetrievalQuery RetrievalQuery;
	RetrievalQuery.ProjectId = ActiveProjectId;
	RetrievalQuery.ThreadId = ActiveThreadId;
	RetrievalQuery.QueryText = UserMessageForComplexity;
	RetrievalService->StartPrefetch(RetrievalQuery, TurnKey);
}

void FUnrealAiContextService::CancelRetrievalPrefetchForThread(const FString& ProjectId, const FString& ThreadId)
{
	if (!RetrievalService)
	{
		return;
	}
	RetrievalService->CancelPrefetchForThread(ProjectId, ThreadId);
}

FAgentContextBuildResult FUnrealAiContextService::BuildContextWindow(const FAgentContextBuildOptions& Options)
{
	FAgentContextBuildResult Result;
	const bool bVerbose = Options.bVerboseContextBuild;
	auto AddTraceLine = [&Result, bVerbose](const FString& Line)
	{
		if (bVerbose)
		{
			Result.VerboseTraceLines.Add(Line);
		}
	};
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		Result.Warnings.Add(TEXT("No active thread — call LoadOrCreate first."));
		return Result;
	}

	const FAgentContextState* Found = FindState(ActiveProjectId, ActiveThreadId);
	if (!Found)
	{
		Result.Warnings.Add(TEXT("No context state for active thread."));
		return Result;
	}

	FAgentContextState Working = *Found;
	{
		FString ParentTid;
		FString NodeSuffix;
		if (UnrealAiCtxPlanChildPriv::TrySplitPlanChildThreadId(ActiveThreadId, ParentTid, NodeSuffix))
		{
			if (const FAgentContextState* ParentState = FindState(ActiveProjectId, ParentTid))
			{
				if (!ParentState->ActivePlanDagJson.IsEmpty())
				{
					Working.ActivePlanDagJson = ParentState->ActivePlanDagJson;
				}
				Working.PlanNodeStatusById = ParentState->PlanNodeStatusById;
				Working.PlanNodeSummaryById = ParentState->PlanNodeSummaryById;
			}
		}
	}
	const int32 PreModeToolResultsCount = Working.ToolResults.Num();
	UnrealAiAgentContextFormat::ApplyModeToStateForBuild(Working, Options);
	if (bVerbose)
	{
		const FString ModeStr = [&Options]()
		{
			switch (Options.Mode)
			{
			case EUnrealAiAgentMode::Ask: return TEXT("ask");
			case EUnrealAiAgentMode::Agent: return TEXT("agent");
			case EUnrealAiAgentMode::Plan: return TEXT("plan");
			default: return TEXT("unknown");
			}
		}();
		AddTraceLine(FString::Printf(TEXT("BuildContextWindow verbose: mode=%s includeAttachments=%d includeToolResults=%d includeEditorSnapshot=%d modelSupportsImages=%d"),
			*ModeStr,
			Options.bIncludeAttachments ? 1 : 0,
			Options.bIncludeToolResults ? 1 : 0,
			Options.bIncludeEditorSnapshot ? 1 : 0,
			Options.bModelSupportsImages ? 1 : 0));
		AddTraceLine(FString::Printf(TEXT("State snapshot (before formatting): attachments=%d toolResults=%d preModeToolResults=%d snapshotSet=%d"),
			Working.Attachments.Num(),
			Working.ToolResults.Num(),
			PreModeToolResultsCount,
			Found->EditorSnapshot.IsSet() ? 1 : 0));
	}

	if (Options.bIncludeAttachments && !Options.bModelSupportsImages)
	{
		TArray<FContextAttachment> Kept;
		Kept.Reserve(Working.Attachments.Num());
		int32 DroppedImages = 0;
		for (const FContextAttachment& A : Working.Attachments)
		{
			if (UnrealAiEditorContextQueries::IsImageLikeAttachment(A))
			{
				++DroppedImages;
				const FString Display = !A.Label.IsEmpty() ? A.Label : A.Payload;
				if (bVerbose)
				{
					AddTraceLine(FString::Printf(
						TEXT("DROP attachment: image-like (reason: model profile has Support images disabled). attachmentType=%d display=\"%s\" payload=\"%s\""),
						static_cast<int32>(A.Type),
						*Display,
						*A.Payload));
				}
				Result.UserVisibleMessages.Add(FString::Printf(
					TEXT("Image attachment was not sent to the model (this model profile has \"Support images\" disabled): %s"),
					*Display));
				continue;
			}
			Kept.Add(A);
		}
		Working.Attachments = MoveTemp(Kept);
		if (bVerbose && DroppedImages > 0)
		{
			AddTraceLine(FString::Printf(TEXT("Image stripping summary: dropped=%d attachments; remaining=%d"), DroppedImages, Working.Attachments.Num()));
		}
	}

	int32 Budget = Options.MaxContextChars;
	if (Working.MaxContextChars > 0)
	{
		Budget = Working.MaxContextChars;
	}

	FAgentContextBuildOptions EffectiveOptions = Options;
	EffectiveOptions.ThreadIdForMemory = ActiveThreadId;
	if (RetrievalService)
	{
		const FUnrealAiRetrievalSettings RetrievalSettings = RetrievalService->LoadSettings();
		EffectiveOptions.ContextAggression = FMath::Clamp(RetrievalSettings.ContextAggression, 0.0f, 1.0f);
	}
	BuildRetrievalUtilityOptions(ActiveProjectId, SessionKey(ActiveProjectId, ActiveThreadId), EffectiveOptions);
	TArray<FUnrealAiRetrievalSnippet> RetrievalSnippets;
	if (RetrievalService && RetrievalService->IsEnabledForProject(ActiveProjectId))
	{
		FUnrealAiRetrievalQuery RetrievalQuery;
		RetrievalQuery.ProjectId = ActiveProjectId;
		RetrievalQuery.ThreadId = ActiveThreadId;
		RetrievalQuery.QueryText = EffectiveOptions.UserMessageForComplexity;
		FUnrealAiRetrievalQueryResult RetrievalResult;
		bool bUsedAsyncPrefetch = false;
		bool bPrefetchReady = false;
		if (!EffectiveOptions.RetrievalTurnKey.IsEmpty())
		{
			const bool bHasPrefetchEntry = RetrievalService->TryConsumePrefetch(
				EffectiveOptions.RetrievalTurnKey,
				RetrievalResult,
				bPrefetchReady);
			if (bHasPrefetchEntry && bPrefetchReady)
			{
				bUsedAsyncPrefetch = true;
				if (bVerbose)
				{
					AddTraceLine(FString::Printf(
						TEXT("Retrieval prefetch consumed turn_key=%s snippets=%d"),
						*EffectiveOptions.RetrievalTurnKey,
						RetrievalResult.Snippets.Num()));
				}
			}
			else if (bHasPrefetchEntry && !bPrefetchReady)
			{
				Result.Warnings.Add(TEXT("Retrieval prefetch not ready; continuing without retrieval snippets."));
				if (bVerbose)
				{
					AddTraceLine(FString::Printf(
						TEXT("Retrieval prefetch miss (not ready) turn_key=%s"),
						*EffectiveOptions.RetrievalTurnKey));
				}
			}
		}
		if (!bUsedAsyncPrefetch && EffectiveOptions.RetrievalTurnKey.IsEmpty())
		{
#if WITH_EDITOR
			const bool bNonBlockingGtRetrieval = IsInGameThread() && !FApp::IsUnattended();
#else
			const bool bNonBlockingGtRetrieval = false;
#endif
			if (bNonBlockingGtRetrieval)
			{
				Result.Warnings.Add(
					TEXT("Retrieval was not applied on this frame (editor stays responsive). "
						 "Use Agent Chat (prefetch) for embedded snippets, or trigger another context build after retrieval prefetch completes."));
				if (bVerbose)
				{
					AddTraceLine(TEXT("Retrieval: skipped synchronous Query on game thread; queued background Query (results not merged this frame)"));
				}
				IUnrealAiRetrievalService* Rs = RetrievalService;
				const FUnrealAiRetrievalQuery QCopy = RetrievalQuery;
				Async(EAsyncExecution::ThreadPool, [Rs, QCopy]()
				{
					if (Rs)
					{
						UnrealAiRetrievalObservability::EmitQueryStart(QCopy);
						const FUnrealAiRetrievalQueryResult BgResult = Rs->Query(QCopy);
						UnrealAiRetrievalObservability::EmitQueryEnd(QCopy, BgResult);
						(void)BgResult;
					}
				});
			}
			else
			{
				UnrealAiRetrievalObservability::EmitQueryStart(RetrievalQuery);
				RetrievalResult = RetrievalService->Query(RetrievalQuery);
				UnrealAiRetrievalObservability::EmitQueryEnd(RetrievalQuery, RetrievalResult);
				Result.UserVisibleMessages.Add(
					UnrealAiProjectTreeSampler::BuildFooterLine(Working.ProjectTreeSummary)
					+ TEXT(" | retrieval=query_sync snippets=")
					+ FString::FromInt(RetrievalResult.Snippets.Num()));
			}
		}
		RetrievalSnippets = RetrievalResult.Snippets;
		for (const FString& Warning : RetrievalResult.Warnings)
		{
			Result.Warnings.Add(Warning);
		}
		if (bVerbose)
		{
			AddTraceLine(FString::Printf(TEXT("Retrieval hook enabled: snippets=%d"), RetrievalResult.Snippets.Num()));
		}
	}

	FString Block;
	const UnrealAiContextCandidates::FUnifiedContextBuildResult Unified =
		UnrealAiContextCandidates::BuildUnifiedContext(Working, EffectiveOptions, MemoryService, &RetrievalSnippets, Budget);
	for (const FString& Line : Unified.TraceLines)
	{
		AddTraceLine(Line);
	}
	if (bVerbose)
	{
		int32 ActionableTargetAnchors = 0;
		for (const UnrealAiContextCandidates::FContextCandidateEnvelope& Packed : Unified.Packed)
		{
			if (Packed.Type == UnrealAiContextRankingPolicy::ECandidateType::ToolResult
				&& (Packed.Payload.Contains(TEXT("/Game/")) || Packed.Payload.Contains(TEXT("PersistentLevel."))))
			{
				++ActionableTargetAnchors;
			}
		}
		AddTraceLine(FString::Printf(TEXT("[Ranker] actionable_target_anchors_kept=%d"), ActionableTargetAnchors));
		int32 ActionableTargetAnchorDrops = 0;
		for (const UnrealAiContextCandidates::FContextCandidateEnvelope& Dropped : Unified.Dropped)
		{
			if (Dropped.DropReason == TEXT("pack:budget_anchor_actionable_target"))
			{
				++ActionableTargetAnchorDrops;
			}
		}
		AddTraceLine(FString::Printf(TEXT("[Ranker] actionable_target_anchor_drops=%d"), ActionableTargetAnchorDrops));
	}
	Block = Unified.ContextBlock;
	RegisterPackedEntityState(SessionKey(ActiveProjectId, ActiveThreadId), Unified.Packed);
	UpdateRetrievalUtilityCounters(ActiveProjectId, SessionKey(ActiveProjectId, ActiveThreadId), Unified.Packed, Unified.Dropped);
	{
		const FDateTime PrevUpdated = ProjectTreeByProjectId.Contains(ActiveProjectId)
			? ProjectTreeByProjectId[ActiveProjectId].UpdatedUtc
			: FDateTime::MinValue();
		const FProjectTreeSummary& SharedSummary = UnrealAiProjectTreeSampler::GetOrRefreshProjectSummary(ActiveProjectId, false);
		FProjectTreeSummary Cached = SharedSummary;
		UnrealAiProjectTreeSampler::ApplyRetrievalHintsToSummary(Cached, RetrievalSnippets);
		const bool bRefreshed = Cached.UpdatedUtc != FDateTime::MinValue() && Cached.UpdatedUtc > PrevUpdated;
		ProjectTreeByProjectId.FindOrAdd(ActiveProjectId) = Cached;
		Working.ProjectTreeSummary = Cached;
		if (FAgentContextState* ActiveState = FindOrAddState(ActiveProjectId, ActiveThreadId))
		{
			ActiveState->ProjectTreeSummary = Cached;
		}
		if (bRefreshed)
		{
			ScheduleSave(ActiveProjectId, ActiveThreadId);
			const FString Detail = UnrealAiBackgroundOpsLog::BuildDetailJson(
				TEXT("project_tree_sample"),
				TEXT("ok"),
				ActiveProjectId,
				ActiveThreadId,
				Cached.LastQueryDurationMs,
				[&Cached](const TSharedPtr<FJsonObject>& O)
				{
					O->SetStringField(TEXT("sampler_version"), Cached.SamplerVersion);
					O->SetStringField(TEXT("query_status"), Cached.LastQueryStatus);
					O->SetNumberField(TEXT("top_folder_count"), Cached.TopLevelFolders.Num());
				});
			UnrealAiBackgroundOpsLog::EmitLogLine(TEXT("project_tree_sample"), TEXT("ok"), Cached.LastQueryDurationMs, Detail);
			Result.UserVisibleMessages.Add(FString::Printf(TEXT("Background query ran: %s"), *Detail));
		}
		const FString StartupOpsLine = UnrealAiStartupOpsStatus::BuildCompactLine(
			UnrealAiStartupOpsStatus::BuildStatus(RetrievalService, Cached, ActiveProjectId));
		const bool bStartupBootstrapped = Cached.LastQueryStatus.Contains(TEXT("startup_bootstrap"));
		const bool bFreshStartupSample = Cached.UpdatedUtc != FDateTime::MinValue()
			&& (FDateTime::UtcNow() - Cached.UpdatedUtc).GetTotalMinutes() <= 5.0;
		if (bRefreshed || (bStartupBootstrapped && bFreshStartupSample))
		{
			Result.UserVisibleMessages.Add(StartupOpsLine);
		}
		Block += TEXT("\n\n");
		Block += UnrealAiProjectTreeSampler::BuildContextBlurb(Cached);
	}
	Result.bTruncated = Unified.bTruncated;
	Result.Warnings.Append(Unified.Warnings);
	AddTraceLine(TEXT("[Ranker] emission=unified"));
	if (UnrealAiContextDecisionLogger::ShouldLogDecisions(Options.bVerboseContextBuild))
	{
		const FString InvocationReason = !Options.ContextBuildInvocationReason.IsEmpty()
			? Options.ContextBuildInvocationReason
			: TEXT("unspecified");
		UnrealAiContextDecisionLogger::WriteDecisionLog(
			ActiveProjectId,
			ActiveThreadId,
			InvocationReason,
			EffectiveOptions,
			Budget,
			Unified,
			Block);
	}
	const int32 RawLen = Block.Len();
	if (bVerbose)
	{
		auto CountOccurrences = [](const FString& Text, const FString& Needle) -> int32
		{
			int32 Count = 0;
			int32 Pos = 0;
			while (true)
			{
				const int32 FoundPos = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
				if (FoundPos == INDEX_NONE)
				{
					return Count;
				}
				++Count;
				Pos = FoundPos + Needle.Len();
			}
		};

		const int32 KeptAttachmentsByMarker = Options.bIncludeAttachments
			? CountOccurrences(Block, TEXT("### Attachment ("))
			: 0;
		const int32 KeptToolsByMarker = (Options.bIncludeToolResults && Options.Mode != EUnrealAiAgentMode::Ask)
			? CountOccurrences(Block, TEXT("### Tool: "))
			: 0;
		const bool bSnapshotIncluded = Options.bIncludeEditorSnapshot
			&& Block.Contains(TEXT("### Editor snapshot"));

		AddTraceLine(FString::Printf(TEXT("Budget trim: budgetChars=%d rawBlockChars=%d finalBlockChars=%d truncated=%d"),
			Budget,
			RawLen,
			Block.Len(),
			Result.bTruncated ? 1 : 0));
		AddTraceLine(FString::Printf(TEXT("Budget trim kept markers (approx): attachments=%d/%d tools=%d/%d snapshotIncluded=%d"),
			KeptAttachmentsByMarker,
			Working.Attachments.Num(),
			KeptToolsByMarker,
			Working.ToolResults.Num(),
			bSnapshotIncluded ? 1 : 0));

		// Mode-driven drops
		if (Options.Mode == EUnrealAiAgentMode::Ask && PreModeToolResultsCount > 0)
		{
			AddTraceLine(FString::Printf(TEXT("DROP (mode-driven): ask mode omitted tool results: preModeToolResults=%d keptInContext=%d"),
				PreModeToolResultsCount,
				Working.ToolResults.Num()));
		}
	}

	if (!Options.StaticSystemPrefix.IsEmpty())
	{
		Result.SystemOrDeveloperBlock = Options.StaticSystemPrefix;
	}

	Result.ContextBlock = MoveTemp(Block);

	Result.ActiveTodoSummaryText = UnrealAiFormatActiveTodoSummary(Working.ActiveTodoPlanJson, Working.TodoStepsDone);

	if (Options.Mode == EUnrealAiAgentMode::Ask && Found->ToolResults.Num() > 0)
	{
		Result.Warnings.Add(TEXT("Ask mode: tool results omitted from context."));
	}

	return Result;
}

void FUnrealAiContextService::SaveNow(const FString& ProjectId, const FString& ThreadId)
{
	FlushSave(ProjectId, ThreadId);
}

const FAgentContextState* FUnrealAiContextService::GetState(const FString& ProjectId, const FString& ThreadId) const
{
	return FindState(ProjectId, ThreadId);
}

void FUnrealAiContextService::ScheduleSave(const FString& ProjectId, const FString& ThreadId)
{
	if (!Persistence)
	{
		return;
	}
	const FString Key = SessionKey(ProjectId, ThreadId);
	PendingSaveKeys.Add(Key);

	if (!SaveTickerHandle.IsValid())
	{
		SaveTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda(
				[this](float) -> bool
				{
					TSet<FString> Copy = MoveTemp(PendingSaveKeys);
					PendingSaveKeys.Reset();
					for (const FString& K : Copy)
					{
						int32 Bar = INDEX_NONE;
						if (K.FindChar(TEXT('|'), Bar))
						{
							const FString Pid = K.Left(Bar);
							const FString Tid = K.Mid(Bar + 1);
							FlushSave(Pid, Tid);
						}
					}
					SaveTickerHandle = FTSTicker::FDelegateHandle();
					return false;
				}),
			0.25f);
	}
}

void FUnrealAiContextService::FlushSave(const FString& ProjectId, const FString& ThreadId)
{
	FlushSaveBySessionKey(SessionKey(ProjectId, ThreadId));
}

void FUnrealAiContextService::FlushSaveBySessionKey(const FString& Key)
{
	if (!Persistence)
	{
		return;
	}
	const FAgentContextState* S = Sessions.Find(Key);
	if (!S)
	{
		return;
	}
	int32 Bar = INDEX_NONE;
	if (!Key.FindChar(TEXT('|'), Bar))
	{
		return;
	}
	const FString Pid = Key.Left(Bar);
	const FString Tid = Key.Mid(Bar + 1);
	FString Json;
	if (UnrealAiAgentContextJson::StateToJson(*S, Json))
	{
		Persistence->SaveThreadContextJson(Pid, Tid, Json);
	}
}

void FUnrealAiContextService::FlushAllSessionsToDisk()
{
	for (const auto& Pair : Sessions)
	{
		FlushSaveBySessionKey(Pair.Key);
	}
	TArray<FString> ProjectIds;
	RetrievalStateByProject.GetKeys(ProjectIds);
	for (const FString& ProjectId : ProjectIds)
	{
		SaveProjectRetrievalState(ProjectId);
	}
}

void FUnrealAiContextService::WipeAllSessionsInMemory()
{
	if (SaveTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SaveTickerHandle);
		SaveTickerHandle.Reset();
	}
	PendingSaveKeys.Reset();
	Sessions.Reset();
	RetrievalUtilityBySession.Reset();
	LastPackedCanonicalRefsBySessionByEntity.Reset();
	RetrievalStateByProject.Reset();
	ActiveProjectId.Reset();
	ActiveThreadId.Reset();
}

bool FUnrealAiContextService::IsBudgetOrCapDropReason(const FString& DropReason)
{
	return DropReason == TEXT("pack:budget")
		|| DropReason == TEXT("pack:soft_budget")
		|| DropReason == TEXT("pack:per_type_cap")
		|| DropReason == TEXT("pack:max_candidates")
		|| DropReason == TEXT("pack:budget_anchor_actionable_target");
}

void FUnrealAiContextService::UpdateRetrievalUtilityCounters(
	const FString& ProjectId,
	const FString& SessionId,
	const TArray<UnrealAiContextCandidates::FContextCandidateEnvelope>& Packed,
	const TArray<UnrealAiContextCandidates::FContextCandidateEnvelope>& Dropped)
{
	if (SessionId.IsEmpty() || ProjectId.IsEmpty())
	{
		return;
	}

	TMap<FString, int32> HitByEntity;
	TMap<FString, int32> KeptByEntity;
	TMap<FString, int32> BudgetDropByEntity;

	auto CountCandidate = [&HitByEntity, &KeptByEntity, &BudgetDropByEntity](const UnrealAiContextCandidates::FContextCandidateEnvelope& Candidate, const bool bKept)
	{
		if (Candidate.Type != UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
		{
			return;
		}
		if (Candidate.EntityId.IsEmpty())
		{
			return;
		}
		HitByEntity.FindOrAdd(Candidate.EntityId) += 1;
		if (bKept)
		{
			KeptByEntity.FindOrAdd(Candidate.EntityId) += 1;
			return;
		}
		if (IsBudgetOrCapDropReason(Candidate.DropReason))
		{
			BudgetDropByEntity.FindOrAdd(Candidate.EntityId) += 1;
		}
	};

	for (const UnrealAiContextCandidates::FContextCandidateEnvelope& Candidate : Packed)
	{
		CountCandidate(Candidate, true);
	}
	for (const UnrealAiContextCandidates::FContextCandidateEnvelope& Candidate : Dropped)
	{
		CountCandidate(Candidate, false);
	}

	if (HitByEntity.Num() == 0)
	{
		return;
	}

	TMap<FString, FRetrievalEntityUtilityCounters>& SessionCounters = RetrievalUtilityBySession.FindOrAdd(SessionId);
	FProjectRetrievalState& ProjectState = RetrievalStateByProject.FindOrAdd(ProjectId);
	TArray<FString> PackedEntities;
	for (const UnrealAiContextCandidates::FContextCandidateEnvelope& Candidate : Packed)
	{
		if (Candidate.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet && !Candidate.EntityId.IsEmpty())
		{
			PackedEntities.AddUnique(Candidate.EntityId);
		}
	}
	for (int32 i = 0; i < PackedEntities.Num(); ++i)
	{
		for (int32 j = 0; j < PackedEntities.Num(); ++j)
		{
			if (i == j)
			{
				continue;
			}
			ProjectState.EdgeWeightsByEntity.FindOrAdd(PackedEntities[i]).FindOrAdd(PackedEntities[j]) += 1;
		}
	}

	constexpr float Decay = 0.85f;
	constexpr float KeepReward = 1.0f;
	constexpr float ActionReward = 1.5f;
	constexpr float BudgetDropPenalty = 0.25f;

	TArray<FString> Keys;
	HitByEntity.GetKeys(Keys);
	for (const FString& EntityId : Keys)
	{
		FRetrievalEntityUtilityCounters& Counters = SessionCounters.FindOrAdd(EntityId);
		FRetrievalEntityUtilityCounters& GlobalCounters = ProjectState.CountersByEntity.FindOrAdd(EntityId);
		const int32 Hits = HitByEntity.FindRef(EntityId);
		const int32 Keeps = KeptByEntity.FindRef(EntityId);
		const int32 BudgetDrops = BudgetDropByEntity.FindRef(EntityId);
		const int32 ActionRefs = Counters.ActionRefCount;

		Counters.RetrievalHitCount += Hits;
		Counters.KeptCount += Keeps;
		Counters.BudgetDropCount += BudgetDrops;
		Counters.UtilityScore = (Counters.UtilityScore * Decay)
			+ (KeepReward * static_cast<float>(Keeps))
			+ (ActionReward * static_cast<float>(ActionRefs))
			- (BudgetDropPenalty * static_cast<float>(BudgetDrops));
		Counters.StaleTurns = Keeps > 0 ? 0 : (Counters.StaleTurns + 1);
		Counters.ActionRefCount = 0;

		GlobalCounters.RetrievalHitCount += Hits;
		GlobalCounters.KeptCount += Keeps;
		GlobalCounters.BudgetDropCount += BudgetDrops;
		GlobalCounters.ActionRefCount += ActionRefs;
		GlobalCounters.UtilityScore = (GlobalCounters.UtilityScore * Decay)
			+ (KeepReward * static_cast<float>(Keeps))
			+ (ActionReward * static_cast<float>(ActionRefs))
			- (BudgetDropPenalty * static_cast<float>(BudgetDrops));
		GlobalCounters.StaleTurns = Keeps > 0 ? 0 : (GlobalCounters.StaleTurns + 1);
	}
	RefreshHeadSetForProject(ProjectId);
	ProjectState.UpdatedUtc = FDateTime::UtcNow();
	SaveProjectRetrievalState(ProjectId);
}

void FUnrealAiContextService::BuildRetrievalUtilityOptions(
	const FString& ProjectId,
	const FString& SessionId,
	FAgentContextBuildOptions& InOutOptions) const
{
	InOutOptions.RetrievalUtilityMultiplierByEntity.Reset();
	InOutOptions.RetrievalLongTailEntityFloor.Reset();
	InOutOptions.RetrievalHeadEntitySet.Reset();
	InOutOptions.RetrievalNeighborsByEntity.Reset();
	InOutOptions.RetrievalLongTailFloorCount = 0;

	if (SessionId.IsEmpty())
	{
		return;
	}
	const TMap<FString, FRetrievalEntityUtilityCounters>* SessionCounters = RetrievalUtilityBySession.Find(SessionId);
	if (!SessionCounters || SessionCounters->Num() == 0)
	{
		return;
	}

	struct FEntityAndScore
	{
		FString EntityId;
		float UtilityScore = 0.f;
	};
	TArray<FEntityAndScore> TailFolders;
	TArray<FEntityAndScore> TailAssetFamilies;

	for (const TPair<FString, FRetrievalEntityUtilityCounters>& Pair : *SessionCounters)
	{
		const FString& EntityId = Pair.Key;
		const FRetrievalEntityUtilityCounters& C = Pair.Value;

		const float ClampedUtility = FMath::Clamp(C.UtilityScore, 0.0f, 1.0f);
		const float UtilityMultiplier = (C.UtilityScore < 0.15f) ? 0.2f : ClampedUtility;
		InOutOptions.RetrievalUtilityMultiplierByEntity.Add(EntityId, UtilityMultiplier);

		// Stale candidates form the long-tail recovery floor.
		if (C.UtilityScore < 0.15f && C.StaleTurns >= 3)
		{
			if (EntityId.StartsWith(TEXT("folder:")))
			{
				TailFolders.Add({ EntityId, C.UtilityScore });
			}
			else if (EntityId.StartsWith(TEXT("asset_family:")))
			{
				TailAssetFamilies.Add({ EntityId, C.UtilityScore });
			}
		}
	}

	auto SortAscByUtility = [](const FEntityAndScore& A, const FEntityAndScore& B)
	{
		if (!FMath::IsNearlyEqual(A.UtilityScore, B.UtilityScore))
		{
			return A.UtilityScore < B.UtilityScore;
		}
		return A.EntityId < B.EntityId;
	};
	TailFolders.Sort(SortAscByUtility);
	TailAssetFamilies.Sort(SortAscByUtility);

	constexpr int32 FolderFloor = 8;
	constexpr int32 AssetFamilyFloor = 4;
	for (int32 i = 0; i < TailFolders.Num() && i < FolderFloor; ++i)
	{
		InOutOptions.RetrievalLongTailEntityFloor.Add(TailFolders[i].EntityId);
	}
	for (int32 i = 0; i < TailAssetFamilies.Num() && i < AssetFamilyFloor; ++i)
	{
		InOutOptions.RetrievalLongTailEntityFloor.Add(TailAssetFamilies[i].EntityId);
	}
	InOutOptions.RetrievalLongTailFloorCount = InOutOptions.RetrievalLongTailEntityFloor.Num();

	if (!ProjectId.IsEmpty())
	{
		if (const FProjectRetrievalState* ProjectState = RetrievalStateByProject.Find(ProjectId))
		{
			InOutOptions.RetrievalHeadEntitySet = ProjectState->HeadEntitySet;
			for (const TPair<FString, TMap<FString, int32>>& Pair : ProjectState->EdgeWeightsByEntity)
			{
				TArray<TPair<FString, int32>> Sorted;
				for (const TPair<FString, int32>& Edge : Pair.Value)
				{
					Sorted.Add(Edge);
				}
				Sorted.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
				{
					if (A.Value != B.Value)
					{
						return A.Value > B.Value;
					}
					return A.Key < B.Key;
				});
				TArray<FString> Neighbors;
				for (int32 i = 0; i < Sorted.Num() && i < 8; ++i)
				{
					Neighbors.Add(Sorted[i].Key);
				}
				if (Neighbors.Num() > 0)
				{
					InOutOptions.RetrievalNeighborsByEntity.Add(Pair.Key, MoveTemp(Neighbors));
				}
			}
		}
	}
}

void FUnrealAiContextService::RegisterPackedEntityState(
	const FString& SessionId,
	const TArray<UnrealAiContextCandidates::FContextCandidateEnvelope>& Packed)
{
	if (SessionId.IsEmpty())
	{
		return;
	}
	TMap<FString, TSet<FString>> PackedRefsByEntity;
	for (const UnrealAiContextCandidates::FContextCandidateEnvelope& Candidate : Packed)
	{
		if (Candidate.Type != UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet || Candidate.EntityId.IsEmpty())
		{
			continue;
		}
		TSet<FString>& EntityRefs = PackedRefsByEntity.FindOrAdd(Candidate.EntityId);
		ExtractCanonicalActionRefs(Candidate.SourceId, EntityRefs);
		ExtractCanonicalActionRefs(Candidate.Payload, EntityRefs);
	}
	LastPackedCanonicalRefsBySessionByEntity.Add(SessionId, MoveTemp(PackedRefsByEntity));
}

void FUnrealAiContextService::RegisterActionReferencesFromToolResult(
	const FString& ProjectId,
	const FString& SessionId,
	const FString& ToolResultText)
{
	if (ProjectId.IsEmpty() || SessionId.IsEmpty() || ToolResultText.IsEmpty())
	{
		return;
	}
	TSet<FString> Refs;
	ExtractCanonicalActionRefs(ToolResultText, Refs);
	if (Refs.Num() == 0)
	{
		return;
	}
	const TMap<FString, TSet<FString>>* PackedEntityRefs = LastPackedCanonicalRefsBySessionByEntity.Find(SessionId);
	if (!PackedEntityRefs)
	{
		return;
	}
	TMap<FString, FRetrievalEntityUtilityCounters>& SessionCounters = RetrievalUtilityBySession.FindOrAdd(SessionId);
	FProjectRetrievalState& ProjectState = RetrievalStateByProject.FindOrAdd(ProjectId);
	for (const TPair<FString, TSet<FString>>& Pair : *PackedEntityRefs)
	{
		const FString& EntityId = Pair.Key;
		const TSet<FString>& EntityRefs = Pair.Value;
		bool bMatched = false;
		for (const FString& Ref : Refs)
		{
			if (EntityRefs.Contains(Ref))
			{
				bMatched = true;
				break;
			}
		}
		if (!bMatched)
		{
			continue;
		}
		SessionCounters.FindOrAdd(EntityId).ActionRefCount += 1;
		ProjectState.CountersByEntity.FindOrAdd(EntityId).ActionRefCount += 1;
	}
}

void FUnrealAiContextService::RefreshHeadSetForProject(const FString& ProjectId)
{
	FProjectRetrievalState* ProjectState = RetrievalStateByProject.Find(ProjectId);
	if (!ProjectState)
	{
		return;
	}
	struct FRow
	{
		FString EntityId;
		float Utility = 0.f;
		int32 Kept = 0;
		int32 ActionRefs = 0;
	};
	TArray<FRow> Rows;
	int64 TotalKept = 0;
	int64 TotalAction = 0;
	for (const TPair<FString, FRetrievalEntityUtilityCounters>& Pair : ProjectState->CountersByEntity)
	{
		FRow Row;
		Row.EntityId = Pair.Key;
		Row.Utility = Pair.Value.UtilityScore;
		Row.Kept = Pair.Value.KeptCount;
		Row.ActionRefs = Pair.Value.ActionRefCount;
		TotalKept += Pair.Value.KeptCount;
		TotalAction += Pair.Value.ActionRefCount;
		Rows.Add(MoveTemp(Row));
	}
	Rows.Sort([](const FRow& A, const FRow& B)
	{
		if (!FMath::IsNearlyEqual(A.Utility, B.Utility))
		{
			return A.Utility > B.Utility;
		}
		return A.EntityId < B.EntityId;
	});
	ProjectState->HeadEntitySet.Reset();
	int64 CumKept = 0;
	int64 CumAction = 0;
	const float CoverageThreshold = 0.8f;
	const int32 FlatFallbackTopN = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Rows.Num()) * 0.02f));
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		ProjectState->HeadEntitySet.Add(Rows[i].EntityId);
		CumKept += Rows[i].Kept;
		CumAction += Rows[i].ActionRefs;
		const float KeptCoverage = TotalKept > 0 ? static_cast<float>(CumKept) / static_cast<float>(TotalKept) : 0.0f;
		const float ActionCoverage = TotalAction > 0 ? static_cast<float>(CumAction) / static_cast<float>(TotalAction) : 0.0f;
		if ((KeptCoverage >= CoverageThreshold && ActionCoverage >= CoverageThreshold)
			|| i + 1 >= FlatFallbackTopN)
		{
			break;
		}
	}
}

FString FUnrealAiContextService::GetProjectRetrievalStatePath(const FString& ProjectId) const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor"), TEXT("ContextUtility"), ProjectId + TEXT("-retrieval-state.json"));
}

void FUnrealAiContextService::SaveProjectRetrievalState(const FString& ProjectId)
{
	if (ProjectId.IsEmpty())
	{
		return;
	}
	const FProjectRetrievalState* ProjectState = RetrievalStateByProject.Find(ProjectId);
	if (!ProjectState)
	{
		return;
	}
	IFileManager::Get().MakeDirectory(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor"), TEXT("ContextUtility")), true);
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("projectId"), ProjectId);
	Root->SetStringField(TEXT("updatedUtc"), ProjectState->UpdatedUtc.ToIso8601());
	TArray<TSharedPtr<FJsonValue>> Entities;
	for (const TPair<FString, FRetrievalEntityUtilityCounters>& Pair : ProjectState->CountersByEntity)
	{
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("entityId"), Pair.Key);
		E->SetNumberField(TEXT("utilityScore"), Pair.Value.UtilityScore);
		E->SetNumberField(TEXT("retrievalHitCount"), Pair.Value.RetrievalHitCount);
		E->SetNumberField(TEXT("keptCount"), Pair.Value.KeptCount);
		E->SetNumberField(TEXT("budgetDropCount"), Pair.Value.BudgetDropCount);
		E->SetNumberField(TEXT("actionRefCount"), Pair.Value.ActionRefCount);
		E->SetNumberField(TEXT("staleTurns"), Pair.Value.StaleTurns);
		Entities.Add(MakeShared<FJsonValueObject>(E));
	}
	Root->SetArrayField(TEXT("entities"), Entities);
	TArray<TSharedPtr<FJsonValue>> Head;
	for (const FString& EntityId : ProjectState->HeadEntitySet)
	{
		Head.Add(MakeShared<FJsonValueString>(EntityId));
	}
	Root->SetArrayField(TEXT("headEntities"), Head);
	TArray<TSharedPtr<FJsonValue>> Edges;
	for (const TPair<FString, TMap<FString, int32>>& Pair : ProjectState->EdgeWeightsByEntity)
	{
		for (const TPair<FString, int32>& Edge : Pair.Value)
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("from"), Pair.Key);
			E->SetStringField(TEXT("to"), Edge.Key);
			E->SetNumberField(TEXT("weight"), Edge.Value);
			Edges.Add(MakeShared<FJsonValueObject>(E));
		}
	}
	Root->SetArrayField(TEXT("edges"), Edges);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		FFileHelper::SaveStringToFile(Json, *GetProjectRetrievalStatePath(ProjectId), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

void FUnrealAiContextService::LoadProjectRetrievalState(const FString& ProjectId)
{
	if (ProjectId.IsEmpty() || RetrievalStateByProject.Contains(ProjectId))
	{
		return;
	}
	FProjectRetrievalState Loaded;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *GetProjectRetrievalStatePath(ProjectId)))
	{
		RetrievalStateByProject.Add(ProjectId, MoveTemp(Loaded));
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		RetrievalStateByProject.Add(ProjectId, MoveTemp(Loaded));
		return;
	}
	FString UpdatedStr;
	if (Root->TryGetStringField(TEXT("updatedUtc"), UpdatedStr))
	{
		FDateTime::ParseIso8601(*UpdatedStr, Loaded.UpdatedUtc);
	}
	if (const TArray<TSharedPtr<FJsonValue>>* Entities = nullptr; Root->TryGetArrayField(TEXT("entities"), Entities) && Entities)
	{
		for (const TSharedPtr<FJsonValue>& V : *Entities)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid())
			{
				continue;
			}
			FString EntityId;
			if (!O->TryGetStringField(TEXT("entityId"), EntityId) || EntityId.IsEmpty())
			{
				continue;
			}
			FRetrievalEntityUtilityCounters& C = Loaded.CountersByEntity.FindOrAdd(EntityId);
			double Num = 0.0;
			if (O->TryGetNumberField(TEXT("utilityScore"), Num)) C.UtilityScore = static_cast<float>(Num);
			if (O->TryGetNumberField(TEXT("retrievalHitCount"), Num)) C.RetrievalHitCount = static_cast<int32>(Num);
			if (O->TryGetNumberField(TEXT("keptCount"), Num)) C.KeptCount = static_cast<int32>(Num);
			if (O->TryGetNumberField(TEXT("budgetDropCount"), Num)) C.BudgetDropCount = static_cast<int32>(Num);
			if (O->TryGetNumberField(TEXT("actionRefCount"), Num)) C.ActionRefCount = static_cast<int32>(Num);
			if (O->TryGetNumberField(TEXT("staleTurns"), Num)) C.StaleTurns = static_cast<int32>(Num);
		}
	}
	if (const TArray<TSharedPtr<FJsonValue>>* Head = nullptr; Root->TryGetArrayField(TEXT("headEntities"), Head) && Head)
	{
		for (const TSharedPtr<FJsonValue>& V : *Head)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
			{
				Loaded.HeadEntitySet.Add(S);
			}
		}
	}
	if (const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr; Root->TryGetArrayField(TEXT("edges"), Edges) && Edges)
	{
		for (const TSharedPtr<FJsonValue>& V : *Edges)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid())
			{
				continue;
			}
			FString From;
			FString To;
			double Weight = 0.0;
			if (!O->TryGetStringField(TEXT("from"), From) || !O->TryGetStringField(TEXT("to"), To) || !O->TryGetNumberField(TEXT("weight"), Weight))
			{
				continue;
			}
			Loaded.EdgeWeightsByEntity.FindOrAdd(From).Add(To, static_cast<int32>(Weight));
		}
	}
	RetrievalStateByProject.Add(ProjectId, MoveTemp(Loaded));
}

void FUnrealAiContextService::ExtractCanonicalActionRefs(const FString& Text, TSet<FString>& OutRefs)
{
	if (Text.IsEmpty())
	{
		return;
	}
	int32 SearchFrom = 0;
	while (SearchFrom < Text.Len())
	{
		const int32 Pos = Text.Find(TEXT("/Game/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
		if (Pos == INDEX_NONE)
		{
			break;
		}
		int32 End = Pos;
		while (End < Text.Len())
		{
			const TCHAR C = Text[End];
			if (FChar::IsWhitespace(C) || C == TEXT('"') || C == TEXT('\'') || C == TEXT(',') || C == TEXT(']') || C == TEXT('}'))
			{
				break;
			}
			++End;
		}
		const FString Ref = Text.Mid(Pos, End - Pos);
		if (Ref.Len() >= 6)
		{
			OutRefs.Add(Ref);
		}
		SearchFrom = End;
	}
}
