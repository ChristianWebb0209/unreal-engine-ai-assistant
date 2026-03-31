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

#include "Containers/Ticker.h"
#include "Context/UnrealAiActiveTodoSummary.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/UnrealAiRecentUiTracker.h"
#include "Planning/UnrealAiPlanDag.h"

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
	Sessions.Remove(SessionKey(ProjectId, ThreadId));
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
	ScheduleSave(ActiveProjectId, ActiveThreadId);
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
			UnrealAiRetrievalObservability::EmitQueryStart(RetrievalQuery);
			RetrievalResult = RetrievalService->Query(RetrievalQuery);
			UnrealAiRetrievalObservability::EmitQueryEnd(RetrievalQuery, RetrievalResult);
			Result.UserVisibleMessages.Add(
				UnrealAiProjectTreeSampler::BuildFooterLine(Working.ProjectTreeSummary)
				+ TEXT(" | retrieval=query_sync snippets=")
				+ FString::FromInt(RetrievalResult.Snippets.Num()));
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
	{
		const FDateTime PrevUpdated = ProjectTreeByProjectId.Contains(ActiveProjectId)
			? ProjectTreeByProjectId[ActiveProjectId].UpdatedUtc
			: FDateTime::MinValue();
		const FProjectTreeSummary& SharedSummary = UnrealAiProjectTreeSampler::GetOrRefreshProjectSummary(ActiveProjectId, false);
		FProjectTreeSummary Cached = SharedSummary;
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
	ActiveProjectId.Reset();
	ActiveThreadId.Reset();
}
