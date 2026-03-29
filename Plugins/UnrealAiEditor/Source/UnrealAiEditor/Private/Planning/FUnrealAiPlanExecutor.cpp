#include "Planning/FUnrealAiPlanExecutor.h"

#include "Context/IAgentContextService.h"
#include "HAL/PlatformTime.h"
#include "Misc/UnrealAiWaitTimePolicy.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Templates/Function.h"

namespace UnrealAiPlanExecutorPriv
{
	static FString MakePlannerDagRepairUserText(const FString& OriginalUser, const FString& Err)
	{
		return OriginalUser
			+ TEXT("\n\n---\n[Plan harness] Previous planner output did not parse or validate as unreal_ai.plan_dag.\n")
			+ TEXT("Error: ")
			+ Err
			+ TEXT(
				"\nReturn a single corrected JSON object only: schema unreal_ai.plan_dag, top-level nodes[] with id, title, hint, "
				"and dependsOn or depends_on (string ids). Each dependency must reference an existing node id; the graph must be "
				"acyclic (no self-edges). No markdown fences or prose outside the JSON.\n---");
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
	Harness->RunTurn(PlannerReq, Sink);
}

void FUnrealAiPlanExecutor::OnPlannerFinished(bool bSuccess, const FString& ErrorText, const FString& PlannerText)
{
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
	if (!UnrealAiPlanDag::ValidateDag(Dag, 64, ParseError))
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
	}
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
		Finish(true, FString());
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
	ChildReq.UserText = FString::Printf(
		TEXT("Execute plan node '%s'.\nTitle: %s\nHint: %s"),
		*Node->Id,
		Node->Title.IsEmpty() ? *Node->Id : *Node->Title,
		*Node->Hint);

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
	Harness->RunTurn(ChildReq, Sink);
}

void FUnrealAiPlanExecutor::OnNodeFinished(const FString& NodeId, bool bSuccess, const FString& ErrorText, const FString& AssistantText)
{
	if (ParentSink.IsValid())
	{
		// Node agent RunTurn finished; automation may reset per-segment sync wait before the next node or Finish().
		ParentSink->OnPlanHarnessSubTurnComplete();
	}
	const FString Summary = AssistantText.Left(300);
	if (ContextService)
	{
		ContextService->SetPlanNodeStatus(NodeId, bSuccess ? TEXT("success") : TEXT("failed"), Summary);
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
