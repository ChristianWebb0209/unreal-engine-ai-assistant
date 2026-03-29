#include "Planning/FUnrealAiPlanExecutor.h"

#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "HAL/PlatformTime.h"
#include "Harness/ILlmTransport.h"
#include "Misc/UnrealAiWaitTimePolicy.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Logging/LogMacros.h"
#include "Templates/Function.h"

namespace UnrealAiPlanExecutorPriv
{
	static FString MakePlannerDagRepairUserText(const FString& OriginalUser, const FString& Err)
	{
		return OriginalUser
			+ TEXT("\n\n---\n[Plan harness] Previous planner output did not parse or validate as unreal_ai.plan_dag.\n")
			+ TEXT("Error: ")
			+ Err
			+ FString::Printf(
				TEXT(
					"\nReturn a single corrected JSON object only: schema unreal_ai.plan_dag, top-level nodes[] with id, title, hint, "
					"and dependsOn or depends_on (string ids). Each dependency must reference an existing node id; the graph must be "
					"acyclic (no self-edges). Keep the planner DAG to at most %d nodes. No markdown fences or prose outside the JSON.\n---"),
				UnrealAiWaitTime::PlannerEmittedMaxDagNodes);
	}

	class FCollectingSink final : public IAgentRunSink
	{
	public:
		TFunction<void(bool, const FString&, const FString&)> OnFinished;
		FString AssistantText;
		TWeakPtr<IAgentRunSink> ForwardTarget;
		FString WorkerNodeId;
		bool bForwardStreamToParent = false;

		FString MakePlanCallId(const FString& CallId) const
		{
			if (WorkerNodeId.IsEmpty())
			{
				return CallId;
			}
			const FString Prefix = FString::Printf(TEXT("plan_%s_"), *WorkerNodeId);
			if (CallId.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return CallId;
			}
			return Prefix + CallId;
		}

		void ForwardIfPossible(const TFunctionRef<void(IAgentRunSink&)> Fn)
		{
			if (!bForwardStreamToParent)
			{
				return;
			}
			if (const TSharedPtr<IAgentRunSink> P = ForwardTarget.Pin())
			{
				Fn(*P);
			}
		}

		virtual void OnRunStarted(const FUnrealAiRunIds& Ids) override { (void)Ids; }
		virtual void OnAssistantDelta(const FString& Chunk) override
		{
			AssistantText += Chunk;
			ForwardIfPossible([&](IAgentRunSink& P) { P.OnAssistantDelta(Chunk); });
		}
		virtual void OnThinkingDelta(const FString& Chunk) override
		{
			ForwardIfPossible([&](IAgentRunSink& P) { P.OnThinkingDelta(Chunk); });
		}
		virtual void OnToolCallStarted(const FString& ToolName, const FString& CallId, const FString& ArgumentsJson) override
		{
			ForwardIfPossible([&](IAgentRunSink& P)
			{ P.OnToolCallStarted(ToolName, MakePlanCallId(CallId), ArgumentsJson); });
		}
		virtual void OnToolCallFinished(
			const FString& ToolName,
			const FString& CallId,
			bool bSuccess,
			const FString& ResultPreview,
			const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation) override
		{
			const FString Id = MakePlanCallId(CallId);
			ForwardIfPossible([&](IAgentRunSink& P)
			{ P.OnToolCallFinished(ToolName, Id, bSuccess, ResultPreview, EditorPresentation); });
		}
		virtual void OnEditorBlockingDialogDuringTools(const FString& Summary) override
		{
			ForwardIfPossible([&](IAgentRunSink& P) { P.OnEditorBlockingDialogDuringTools(Summary); });
		}
		virtual void OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint) override
		{
			// Child Agent-mode harness calls OnRunContinuation(LlmRound-1, EffectiveMaxLlmRounds) — second value is
			// the LLM round cap (e.g. 512), not "total plan phases". Forwarding that to the parent mislabels run.jsonl
			// as total_phases_hint=512. Plan phase hints are emitted only from FUnrealAiPlanExecutor (planner + per node).
			if (!WorkerNodeId.IsEmpty())
			{
				return;
			}
			ForwardIfPossible([&](IAgentRunSink& P) { P.OnRunContinuation(PhaseIndex, TotalPhasesHint); });
		}
		virtual void OnTodoPlanEmitted(const FString& Title, const FString& PlanJson) override
		{
			ForwardIfPossible([&](IAgentRunSink& P) { P.OnTodoPlanEmitted(Title, PlanJson); });
		}
		virtual void OnPlanDraftReady(const FString& DagJsonText) override
		{
			ForwardIfPossible([&](IAgentRunSink& P) { P.OnPlanDraftReady(DagJsonText); });
		}
		virtual void OnLlmRequestPreparedForHttp(
			const FUnrealAiAgentTurnRequest& TurnRequest,
			const FGuid& RunId,
			int32 LlmRound,
			int32 EffectiveMaxLlmRounds,
			const FUnrealAiLlmRequest& LlmRequest) override
		{
			ForwardIfPossible([&](IAgentRunSink& P)
			{ P.OnLlmRequestPreparedForHttp(TurnRequest, RunId, LlmRound, EffectiveMaxLlmRounds, LlmRequest); });
		}
		virtual void OnHarnessProgressLog(const FString& Line) override
		{
			ForwardIfPossible([&](IAgentRunSink& P) { P.OnHarnessProgressLog(Line); });
		}
		virtual void OnRunFinished(bool bSuccess, const FString& ErrorMessage) override
		{
			if (OnFinished)
			{
				OnFinished(bSuccess, ErrorMessage, AssistantText);
			}
		}
	};
}

TSharedRef<FUnrealAiPlanExecutor> FUnrealAiPlanExecutor::Start(
	IUnrealAiAgentHarness* InHarness,
	IAgentContextService* InContextService,
	const FUnrealAiAgentTurnRequest& InParentRequest,
	TSharedPtr<IAgentRunSink> InParentSink,
	const FUnrealAiPlanExecutorStartOptions& Options)
{
	TSharedRef<FUnrealAiPlanExecutor> Exec = MakeShared<FUnrealAiPlanExecutor>();
	Exec->Harness = InHarness;
	Exec->ContextService = InContextService;
	Exec->ParentRequest = InParentRequest;
	Exec->ParentSink = MoveTemp(InParentSink);
	Exec->bPauseAfterPlannerForBuild = Options.bPauseAfterPlannerForBuild;
	Exec->bHarnessPlannerOnlyNoExecute = Options.bHarnessPlannerOnlyNoExecute;
	Exec->OriginalPlannerUserText = InParentRequest.UserText;
	Exec->bRunning = true;
	Exec->PlanExecutionPhase = 0;
	Exec->ParentIds.RunId = FGuid::NewGuid();
	Exec->ParentIds.ParentRunId.Invalidate();
	Exec->ParentIds.WorkerIndex = INDEX_NONE;
	Exec->PlanMaxWallMsCached = UnrealAiWaitTime::HarnessPlanMaxWallMs;
	if (Exec->PlanMaxWallMsCached > 0)
	{
		Exec->PlanWallStartSec = FPlatformTime::Seconds();
	}
	if (Exec->ParentSink.IsValid())
	{
		Exec->ParentSink->OnRunStarted(Exec->ParentIds);
	}
	if (InHarness)
	{
		InHarness->NotifyPlanExecutorStarted(Exec.ToSharedPtr());
	}
	if (Exec->ParentSink.IsValid())
	{
		Exec->ParentSink->OnHarnessProgressLog(TEXT("FUnrealAiPlanExecutor::Start -> BeginPlannerTurn (planner HTTP next)"));
	}
	UE_LOG(LogTemp, Display, TEXT("UnrealAi plan: executor Start -> BeginPlannerTurn"));
	Exec->BeginPlannerTurn();
	return Exec;
}

TSharedRef<FUnrealAiPlanExecutor> FUnrealAiPlanExecutor::ResumeExecutionFromDag(
	IUnrealAiAgentHarness* InHarness,
	IAgentContextService* InContextService,
	const FUnrealAiAgentTurnRequest& InParentRequest,
	TSharedPtr<IAgentRunSink> InParentSink,
	const FString& DagJsonText)
{
	TSharedRef<FUnrealAiPlanExecutor> Exec = MakeShared<FUnrealAiPlanExecutor>();
	Exec->Harness = InHarness;
	Exec->ContextService = InContextService;
	Exec->ParentRequest = InParentRequest;
	Exec->ParentSink = MoveTemp(InParentSink);
	Exec->bPauseAfterPlannerForBuild = false;
	Exec->bAwaitingBuild = false;
	Exec->bRunning = true;
	Exec->PlanExecutionPhase = 0;
	Exec->ParentIds.RunId = FGuid::NewGuid();
	Exec->ParentIds.ParentRunId.Invalidate();
	Exec->ParentIds.WorkerIndex = INDEX_NONE;
	Exec->PlanMaxWallMsCached = UnrealAiWaitTime::HarnessPlanMaxWallMs;
	if (Exec->PlanMaxWallMsCached > 0)
	{
		Exec->PlanWallStartSec = FPlatformTime::Seconds();
	}

	FString Err;
	if (!UnrealAiPlanDag::ParseDagJson(DagJsonText, Exec->Dag, Err))
	{
		Exec->Finish(false,
			FString::Printf(
				TEXT("Invalid DAG JSON (parse): %s Dependencies must use dependsOn/depends_on arrays of string ids that exist on nodes."),
				*Err));
		return Exec;
	}
	if (!UnrealAiPlanDag::ValidateDag(Exec->Dag, 64, Err))
	{
		Exec->Finish(false,
			FString::Printf(
				TEXT("DAG validation failed: %s Ensure dependsOn references existing node ids, no self-dependencies, and no cycles."),
				*Err));
		return Exec;
	}
	if (InContextService)
	{
		InContextService->LoadOrCreate(InParentRequest.ProjectId, InParentRequest.ThreadId);
		InContextService->SetActivePlanDag(DagJsonText);
	}
	if (Exec->ParentSink.IsValid())
	{
		Exec->ParentSink->OnRunStarted(Exec->ParentIds);
		const int32 TotalPhases = 1 + Exec->Dag.Nodes.Num();
		Exec->ParentSink->OnRunContinuation(0, TotalPhases);
	}
	if (InHarness)
	{
		InHarness->NotifyPlanExecutorStarted(Exec.ToSharedPtr());
	}
	Exec->BeginNextReadyNode();
	return Exec;
}

bool FUnrealAiPlanExecutor::ApplyDagJsonForBuild(const FString& DagJsonText, FString& OutError)
{
	if (!UnrealAiPlanDag::ParseDagJson(DagJsonText, Dag, OutError))
	{
		return false;
	}
	if (!UnrealAiPlanDag::ValidateDag(Dag, 64, OutError))
	{
		return false;
	}
	if (ContextService)
	{
		ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
		ContextService->SetActivePlanDag(DagJsonText);
	}
	return true;
}

void FUnrealAiPlanExecutor::ResumeNodeExecution()
{
	if (!bRunning || !bAwaitingBuild)
	{
		return;
	}
	bAwaitingBuild = false;
	// User may spend arbitrary time editing the DAG; restart wall clock when execution resumes.
	if (PlanMaxWallMsCached > 0)
	{
		PlanWallStartSec = FPlatformTime::Seconds();
	}
	if (ParentSink.IsValid())
	{
		const int32 TotalPhases = 1 + Dag.Nodes.Num();
		ParentSink->OnRunContinuation(0, TotalPhases);
	}
	BeginNextReadyNode();
}

void FUnrealAiPlanExecutor::Cancel()
{
	bCancelled = true;
	if (bAwaitingBuild)
	{
		bAwaitingBuild = false;
		Finish(false, TEXT("Plan cancelled."));
		return;
	}
	if (Harness)
	{
		Harness->CancelTurn();
	}
}

bool FUnrealAiPlanExecutor::CheckPlanWallBudgetOrFinish()
{
	if (PlanMaxWallMsCached == 0 || !bRunning)
	{
		return true;
	}
	const double ElapsedMs = (FPlatformTime::Seconds() - PlanWallStartSec) * 1000.0;
	if (ElapsedMs <= static_cast<double>(PlanMaxWallMsCached))
	{
		return true;
	}
	UE_LOG(LogTemp, Warning, TEXT("UnrealAi plan: wall budget exceeded (elapsed %.0f ms, limit %u ms)."), ElapsedMs, PlanMaxWallMsCached);
	if (Harness && Harness->IsTurnInProgress())
	{
		Harness->CancelTurn();
	}
	Finish(false, FString::Printf(
		TEXT("Plan exceeded maximum wall time (%u ms). Reduce DAG scope or adjust HarnessPlanMaxWallMs in UnrealAiWaitTimePolicy.h."),
		PlanMaxWallMsCached));
	return false;
}

void FUnrealAiPlanExecutor::BeginPlannerTurn()
{
	if (!CheckPlanWallBudgetOrFinish())
	{
		return;
	}
	if (!Harness)
	{
		Finish(false, TEXT("Harness unavailable for plan planner."));
		return;
	}
	FUnrealAiAgentTurnRequest PlannerReq = ParentRequest;
	PlannerReq.Mode = EUnrealAiAgentMode::Plan;
	if (!PendingPlannerUserTextOverride.IsEmpty())
	{
		PlannerReq.UserText = PendingPlannerUserTextOverride;
		PendingPlannerUserTextOverride.Reset();
	}
	const TSharedRef<UnrealAiPlanExecutorPriv::FCollectingSink> Sink = MakeShared<UnrealAiPlanExecutorPriv::FCollectingSink>();
	Sink->ForwardTarget = ParentSink;
	Sink->bForwardStreamToParent = true;
	const TWeakPtr<FUnrealAiPlanExecutor> WeakExec = AsShared();
	Sink->OnFinished = [WeakExec](bool bSuccess, const FString& ErrorText, const FString& Text)
	{
		if (const TSharedPtr<FUnrealAiPlanExecutor> Self = WeakExec.Pin())
		{
			Self->OnPlannerFinished(bSuccess, ErrorText, Text);
		}
	};
	if (ParentSink.IsValid())
	{
		ParentSink->OnHarnessProgressLog(TEXT("BeginPlannerTurn: calling Harness->RunTurn (planner pass, mode=plan)"));
	}
	UE_LOG(LogTemp, Display, TEXT("UnrealAi plan: BeginPlannerTurn -> RunTurn (planner)"));
	Harness->RunTurn(PlannerReq, Sink);
}

void FUnrealAiPlanExecutor::OnPlannerFinished(bool bSuccess, const FString& ErrorText, const FString& PlannerText)
{
	if (ParentSink.IsValid())
	{
		ParentSink->OnHarnessProgressLog(FString::Printf(
			TEXT("OnPlannerFinished: success=%s err_len=%d dag_text_len=%d"),
			bSuccess ? TEXT("1") : TEXT("0"),
			ErrorText.Len(),
			PlannerText.Len()));
	}
	UE_LOG(LogTemp, Display, TEXT("UnrealAi plan: OnPlannerFinished success=%s"), bSuccess ? TEXT("yes") : TEXT("no"));
	if (bCancelled)
	{
		Finish(false, TEXT("Plan cancelled."));
		return;
	}

	if (ParentSink.IsValid())
	{
		const int32 TagStart = PlannerText.Find(TEXT("<chat-name"), ESearchCase::IgnoreCase);
		if (TagStart >= 0)
		{
			const int32 TagEnd = PlannerText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, TagStart);
			if (TagEnd >= 0 && TagEnd >= TagStart)
			{
				const FString TokenChunk = PlannerText.Mid(TagStart, (TagEnd - TagStart) + 1);
				if (!TokenChunk.IsEmpty())
				{
					ParentSink->OnAssistantDelta(TokenChunk);
				}
			}
		}
	}

	if (!bSuccess)
	{
		Finish(false, ErrorText.IsEmpty() ? TEXT("Planner turn failed.") : ErrorText);
		return;
	}

	FString ParseError;
	if (!UnrealAiPlanDag::ParseDagJson(PlannerText, Dag, ParseError))
	{
		if (!bPlannerDagRepairConsumed)
		{
			bPlannerDagRepairConsumed = true;
			PendingPlannerUserTextOverride = UnrealAiPlanExecutorPriv::MakePlannerDagRepairUserText(OriginalPlannerUserText, ParseError);
			BeginPlannerTurn();
			return;
		}
		Finish(false,
			FString::Printf(
				TEXT("Planner output is still invalid JSON after one repair attempt: %s"),
				*ParseError));
		return;
	}
	if (!UnrealAiPlanDag::ValidateDag(Dag, UnrealAiWaitTime::PlannerEmittedMaxDagNodes, ParseError))
	{
		if (!bPlannerDagRepairConsumed)
		{
			bPlannerDagRepairConsumed = true;
			PendingPlannerUserTextOverride = UnrealAiPlanExecutorPriv::MakePlannerDagRepairUserText(OriginalPlannerUserText, ParseError);
			BeginPlannerTurn();
			return;
		}
		Finish(false,
			FString::Printf(
				TEXT("Planner DAG still failed validation after one repair attempt: %s Each dependsOn must reference an existing node id; graph must be acyclic."),
				*ParseError));
		return;
	}
	if (ContextService)
	{
		ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
		ContextService->SetActivePlanDag(PlannerText);
	}
	if (bHarnessPlannerOnlyNoExecute)
	{
		if (ParentSink.IsValid())
		{
			ParentSink->OnAssistantDelta(FString::Printf(
				TEXT("Plan ready (harness planner-only): %d nodes (nodes not executed).\n"),
				Dag.Nodes.Num()));
			const int32 TotalPhases = 1 + Dag.Nodes.Num();
			ParentSink->OnRunContinuation(0, TotalPhases);
			ParentSink->OnPlanHarnessSubTurnComplete();
		}
		Finish(true, FString());
		return;
	}
	if (bPauseAfterPlannerForBuild)
	{
		bAwaitingBuild = true;
		if (ParentSink.IsValid())
		{
			ParentSink->OnAssistantDelta(FString::Printf(
				TEXT("Plan ready (%d nodes). Review or edit the DAG below, then click Build to run.\n"),
				Dag.Nodes.Num()));
			ParentSink->OnPlanDraftReady(PlannerText);
		}
		return;
	}
	if (ParentSink.IsValid())
	{
		ParentSink->OnAssistantDelta(FString::Printf(TEXT("Plan ready: %d nodes. Executing serially.\n"), Dag.Nodes.Num()));
		const int32 TotalPhases = 1 + Dag.Nodes.Num();
		ParentSink->OnRunContinuation(0, TotalPhases);
		// Planner harness RunTurn finished; automation may reset per-segment sync wait before node execution.
		ParentSink->OnPlanHarnessSubTurnComplete();
		ParentSink->OnHarnessProgressLog(FString::Printf(
			TEXT("DAG parsed: %d nodes; OnPlanHarnessSubTurnComplete fired -> scenario runner should reset sync window; next BeginNextReadyNode"),
			Dag.Nodes.Num()));
	}
	UE_LOG(LogTemp, Display, TEXT("UnrealAi plan: DAG valid (%d nodes) -> BeginNextReadyNode"), Dag.Nodes.Num());
	if (!CheckPlanWallBudgetOrFinish())
	{
		return;
	}
	BeginNextReadyNode();
}

void FUnrealAiPlanExecutor::BeginNextReadyNode()
{
	if (bCancelled)
	{
		Finish(false, TEXT("Plan cancelled."));
		return;
	}
	if (!CheckPlanWallBudgetOrFinish())
	{
		return;
	}
	if (ContextService)
	{
		ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
		if (Harness && !Harness->IsTurnInProgress())
		{
			ContextService->ClearPlanStaleRunningMarkers(ParentRequest.ProjectId, ParentRequest.ThreadId);
		}
	}
	const FAgentContextState* State = nullptr;
	TMap<FString, FString> Statuses;
	if (ContextService)
	{
		State = ContextService->GetState(ParentRequest.ProjectId, ParentRequest.ThreadId);
		if (State)
		{
			Statuses = State->PlanNodeStatusById;
		}
	}
	UnrealAiPlanDag::GetReadyNodeIds(Dag, Statuses, ReadyNodeIds);
	if (ReadyNodeIds.Num() == 0)
	{
		FinishWhenDagFullyResolved();
		return;
	}
	const FString NodeId = ReadyNodeIds[0];
	const FUnrealAiDagNode* Node = Dag.Nodes.FindByPredicate([&NodeId](const FUnrealAiDagNode& N) { return N.Id == NodeId; });
	if (!Node)
	{
		Finish(false, TEXT("Ready node lookup failed."));
		return;
	}
	if (ContextService)
	{
		ContextService->SetPlanNodeStatus(NodeId, TEXT("running"));
	}

	++PlanExecutionPhase;
	if (ParentSink.IsValid())
	{
		const int32 TotalPhases = 1 + Dag.Nodes.Num();
		ParentSink->OnRunContinuation(PlanExecutionPhase, TotalPhases);
	}

	FUnrealAiAgentTurnRequest ChildReq = ParentRequest;
	ChildReq.Mode = EUnrealAiAgentMode::Agent;
	ChildReq.LlmRoundBudgetFloor = FMath::Min(64, FMath::Max(1, UnrealAiWaitTime::PlanNodeMaxLlmRounds));
	ChildReq.ThreadId = FString::Printf(TEXT("%s_plan_%s"), *ParentRequest.ThreadId, *NodeId);
	{
		static constexpr int32 GPlanNodeParentUserTextMaxChars = 4000;
		FString ParentGoal = OriginalPlannerUserText.IsEmpty() ? ParentRequest.UserText : OriginalPlannerUserText;
		ParentGoal.TrimStartAndEndInline();
		if (ParentGoal.Len() > GPlanNodeParentUserTextMaxChars)
		{
			ParentGoal = ParentGoal.Left(GPlanNodeParentUserTextMaxChars) + TEXT("\n[...truncated]");
		}
		const FString NodeTitle = Node->Title.IsEmpty() ? Node->Id : Node->Title;
		ChildReq.UserText = FString::Printf(
			TEXT("## Original request (from user)\n%s\n\n---\n\n## Current plan node\nExecute plan node '%s'.\nTitle: %s\nHint: %s\n\nComplete **this node only**, aligned with the original request above. Do not expand scope beyond the hint unless the user asked for it."),
			*ParentGoal,
			*Node->Id,
			*NodeTitle,
			*Node->Hint);
	}

	const TSharedRef<UnrealAiPlanExecutorPriv::FCollectingSink> Sink = MakeShared<UnrealAiPlanExecutorPriv::FCollectingSink>();
	const TWeakPtr<FUnrealAiPlanExecutor> WeakExec = AsShared();
	Sink->ForwardTarget = ParentSink;
	Sink->WorkerNodeId = NodeId;
	Sink->bForwardStreamToParent = true;
	Sink->OnFinished = [WeakExec, NodeId](bool bSuccess, const FString& Error, const FString& AssistantText)
	{
		if (const TSharedPtr<FUnrealAiPlanExecutor> Self = WeakExec.Pin())
		{
			Self->OnNodeFinished(NodeId, bSuccess, Error, AssistantText);
		}
	};
	if (ParentSink.IsValid())
	{
		ParentSink->OnHarnessProgressLog(FString::Printf(
			TEXT("BeginNextReadyNode: calling Harness->RunTurn for node_id=%s child_thread=%s (agent tools enabled)"),
			*NodeId,
			*ChildReq.ThreadId));
	}
	UE_LOG(LogTemp, Display, TEXT("UnrealAi plan: RunTurn plan_node=%s thread=%s"), *NodeId, *ChildReq.ThreadId);
	Harness->RunTurn(ChildReq, Sink);
}

void FUnrealAiPlanExecutor::OnNodeFinished(const FString& NodeId, bool bSuccess, const FString& ErrorText, const FString& AssistantText)
{
	if (ParentSink.IsValid())
	{
		ParentSink->OnHarnessProgressLog(FString::Printf(
			TEXT("OnNodeFinished: node_id=%s success=%s assistant_len=%d"),
			*NodeId,
			bSuccess ? TEXT("1") : TEXT("0"),
			AssistantText.Len()));
		// Node agent RunTurn finished; automation may reset per-segment sync wait before the next node or Finish().
		ParentSink->OnPlanHarnessSubTurnComplete();
	}
	UE_LOG(LogTemp, Display, TEXT("UnrealAi plan: OnNodeFinished node=%s success=%s"), *NodeId, bSuccess ? TEXT("yes") : TEXT("no"));
	const FString Summary = AssistantText.Left(300);
	if (!bSuccess)
	{
		bAnyPlanNodeFailedThisRun = true;
	}
	if (ContextService)
	{
		// Child node RunTurn uses ThreadId "<parent>_plan_<node>"; context service active session follows that thread.
		// Plan DAG readiness (GetReadyNodeIds) reads PlanNodeStatusById on the parent thread — restore it before updating status.
		ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
		ContextService->SetPlanNodeStatus(NodeId, bSuccess ? TEXT("success") : TEXT("failed"), Summary);
		if (!bSuccess)
		{
			CascadeSkipDependentsAfterFailure(NodeId, ErrorText);
		}
	}
	if (ParentSink.IsValid() && !bSuccess)
	{
		ParentSink->OnAssistantDelta(FString::Printf(
			TEXT("Plan node \"%s\" failed: %s\n"),
			*NodeId,
			*ErrorText));
	}
	BeginNextReadyNode();
}

void FUnrealAiPlanExecutor::CascadeSkipDependentsAfterFailure(const FString& FailedNodeId, const FString& ErrorText)
{
	if (!ContextService || FailedNodeId.IsEmpty())
	{
		return;
	}
	ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
	TArray<FString> Dependents;
	UnrealAiPlanDag::CollectTransitiveDependents(Dag, FailedNodeId, Dependents);
	FString ErrOneLine = ErrorText;
	ErrOneLine.ReplaceInline(TEXT("\r"), TEXT(""));
	ErrOneLine.ReplaceInline(TEXT("\n"), TEXT(" "));
	ErrOneLine.TrimStartAndEndInline();
	if (ErrOneLine.Len() > 180)
	{
		ErrOneLine = ErrOneLine.Left(180) + TEXT("...");
	}
	for (const FString& SkippedId : Dependents)
	{
		ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
		const FAgentContextState* St = ContextService->GetState(ParentRequest.ProjectId, ParentRequest.ThreadId);
		const FString* Prev = St ? St->PlanNodeStatusById.Find(SkippedId) : nullptr;
		if (Prev
			&& (Prev->Equals(TEXT("success"), ESearchCase::IgnoreCase) || Prev->Equals(TEXT("failed"), ESearchCase::IgnoreCase)
				|| Prev->Equals(TEXT("skipped"), ESearchCase::IgnoreCase)))
		{
			continue;
		}
		const FString SkipSummary = ErrOneLine.IsEmpty()
			? FString::Printf(TEXT("skipped: upstream node '%s' failed"), *FailedNodeId)
			: FString::Printf(TEXT("skipped: upstream node '%s' failed (%s)"), *FailedNodeId, *ErrOneLine);
		ContextService->SetPlanNodeStatus(SkippedId, TEXT("skipped"), SkipSummary);
	}
}

void FUnrealAiPlanExecutor::FinishWhenDagFullyResolved()
{
	if (ComputePlanOverallSuccess())
	{
		Finish(true, FString());
	}
	else
	{
		Finish(false, BuildPlanFailureRollupMessage());
	}
}

bool FUnrealAiPlanExecutor::ComputePlanOverallSuccess()
{
	if (bAnyPlanNodeFailedThisRun)
	{
		return false;
	}
	if (!ContextService)
	{
		return true;
	}
	ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
	if (const FAgentContextState* St = ContextService->GetState(ParentRequest.ProjectId, ParentRequest.ThreadId))
	{
		for (const TPair<FString, FString>& Pair : St->PlanNodeStatusById)
		{
			if (Pair.Value.Equals(TEXT("failed"), ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
	}
	return true;
}

FString FUnrealAiPlanExecutor::BuildPlanFailureRollupMessage()
{
	if (!ContextService)
	{
		return TEXT("Plan completed with one or more failed nodes.");
	}
	ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
	const FAgentContextState* St = ContextService->GetState(ParentRequest.ProjectId, ParentRequest.ThreadId);
	if (!St)
	{
		return TEXT("Plan completed with one or more failed nodes.");
	}
	TArray<FString> Parts;
	for (const TPair<FString, FString>& Pair : St->PlanNodeStatusById)
	{
		if (!Pair.Value.Equals(TEXT("failed"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		FString Line = Pair.Key;
		if (const FString* Sum = St->PlanNodeSummaryById.Find(Pair.Key))
		{
			if (!Sum->IsEmpty())
			{
				Line += TEXT(": ");
				Line += Sum->Left(200);
			}
		}
		Parts.Add(Line);
	}
	if (Parts.Num() == 0)
	{
		return TEXT("Plan completed with one or more failed nodes.");
	}
	return FString::Printf(TEXT("Plan completed with failures: %s"), *FString::Join(Parts, TEXT("; ")));
}

void FUnrealAiPlanExecutor::Finish(bool bSuccess, const FString& ErrorText)
{
	if (!bRunning)
	{
		return;
	}
	bRunning = false;
	if (Harness)
	{
		Harness->NotifyPlanExecutorEnded();
	}
	if (ParentSink.IsValid())
	{
		ParentSink->OnRunFinished(bSuccess, ErrorText);
	}
}
