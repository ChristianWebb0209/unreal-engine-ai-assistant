#include "Planning/FUnrealAiPlanExecutor.h"

#include "Async/TaskGraphInterfaces.h"
#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Harness/ILlmTransport.h"
#include "Misc/UnrealAiWaitTimePolicy.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Logging/LogMacros.h"
#include "Templates/Function.h"
#include "UnrealAiEditorModule.h"
#include "UnrealAiEditorSettings.h"

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

	static FString ExtractHarnessReasonCode(const FString& ErrorText)
	{
		const int32 Start = ErrorText.Find(TEXT("[reason="), ESearchCase::IgnoreCase);
		if (Start == INDEX_NONE)
		{
			return FString();
		}
		const int32 ReasonStart = Start + 8;
		const int32 End = ErrorText.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ReasonStart);
		if (End == INDEX_NONE || End <= ReasonStart)
		{
			return FString();
		}
		return ErrorText.Mid(ReasonStart, End - ReasonStart).TrimStartAndEnd();
	}

	static FString MapFailureCategory(const FString& ErrorText)
	{
		const FString ReasonCode = ExtractHarnessReasonCode(ErrorText).ToLower();
		if (ReasonCode.Contains(TEXT("plan_node_repeated_validation")) || ReasonCode.Contains(TEXT("repeated_validation")))
		{
			return TEXT("validation_error");
		}
		if (ReasonCode.Contains(TEXT("plan_node_repeated_tool")) || ReasonCode.Contains(TEXT("tool_budget")))
		{
			return TEXT("tool_budget");
		}
		if (ReasonCode.Contains(TEXT("stream_no_finish")) || ReasonCode.Contains(TEXT("stream_incomplete")))
		{
			return TEXT("stream_incomplete");
		}
		if (ReasonCode.Contains(TEXT("transient")) || ReasonCode.Contains(TEXT("transport")) || ReasonCode.Contains(TEXT("timeout")))
		{
			return TEXT("transient_transport");
		}
		if (ReasonCode.Contains(TEXT("empty_assistant")))
		{
			return TEXT("empty_assistant");
		}

		const FString Lower = ErrorText.ToLower();
		if (Lower.Contains(TEXT("missing required")) || Lower.Contains(TEXT("invalid")) || Lower.Contains(TEXT("validation")))
		{
			return TEXT("validation_error");
		}
		if ((Lower.Contains(TEXT("stream")) && Lower.Contains(TEXT("incomplete"))) || Lower.Contains(TEXT("did not reach complete json")))
		{
			return TEXT("stream_incomplete");
		}
		if (Lower.Contains(TEXT("repeat limit")) || Lower.Contains(TEXT("same tool")) || Lower.Contains(TEXT("tool budget")))
		{
			return TEXT("tool_budget");
		}
		return TEXT("tool_or_runtime_failure");
	}

	static bool ShouldAutoReplanForCategory(const FString& Category)
	{
		return Category == TEXT("stream_incomplete") || Category == TEXT("transient_transport") || Category == TEXT("empty_assistant");
	}

	static FString SummarizeNodeStatusesForPlanner(const FAgentContextState* St)
	{
		if (!St)
		{
			return TEXT("(no context)");
		}
		TArray<FString> Lines;
		for (const TPair<FString, FString>& Pair : St->PlanNodeStatusById)
		{
			FString Line = FString::Printf(TEXT("- %s: %s"), *Pair.Key, *Pair.Value);
			if (const FString* Sum = St->PlanNodeSummaryById.Find(Pair.Key))
			{
				if (!Sum->IsEmpty())
				{
					Line += TEXT(" — ");
					Line += Sum->Left(160);
				}
			}
			Lines.Add(Line);
		}
		if (Lines.Num() == 0)
		{
			return TEXT("(no node statuses yet)");
		}
		Lines.Sort();
		return FString::Join(Lines, TEXT("\n"));
	}

	static FString MakeNodeFailureReplanUserText(
		const FString& OriginalUser,
		const FString& FailedNodeId,
		const FString& ErrorOneLine,
		const FString& SerializedDagJson,
		const FString& StatusSummary)
	{
		FString Err = ErrorOneLine;
		Err.ReplaceInline(TEXT("\r"), TEXT(""));
		Err.ReplaceInline(TEXT("\n"), TEXT(" "));
		Err.TrimStartAndEndInline();
		if (Err.Len() > 400)
		{
			Err = Err.Left(400) + TEXT("...");
		}
		return OriginalUser
			+ TEXT("\n\n---\n[Plan harness] A plan **node failed**. Emit a **revised unreal_ai.plan_dag JSON** with ONLY **new** ")
			  TEXT("nodes for remaining work. Each new node `id` must be new (do not reuse ids of nodes already marked `success` below). ")
			  TEXT("`dependsOn` may reference completed **success** node ids from the prior run **or** other new ids in your output. ")
			  TEXT("Do not depend on the failed node. Keep at most ")
			+ FString::FromInt(UnrealAiWaitTime::PlannerEmittedMaxDagNodes)
			+ TEXT(" **new** nodes.\n")
			  TEXT("Failed node id: `")
			+ FailedNodeId + TEXT("`\nHarness/tool error (one line): ") + Err
			+ TEXT("\n\nCurrent per-node status:\n")
			+ StatusSummary + TEXT("\n\nCurrent DAG JSON (for reference):\n") + SerializedDagJson
			+ TEXT("\n\nReturn a **single JSON object** only: schema unreal_ai.plan_dag, top-level `nodes[]` with id, title, hint, ")
			  TEXT("and dependsOn / depends_on. No markdown fences or prose outside the JSON.\n---");
	}

	static FString MakeNodeFailureReplanRepairUserText(const FString& BasePrompt, const FString& ValidationErr)
	{
		return BasePrompt
			+ TEXT("\n\n---\n[Plan harness] Your previous replan output did not parse or validate.\nError: ")
			+ ValidationErr
			+ FString::Printf(
				TEXT("\nReturn a single corrected JSON object only: schema unreal_ai.plan_dag, new nodes only as instructed above, at most %d new nodes. No markdown fences or prose outside the JSON.\n---"),
				UnrealAiWaitTime::PlannerEmittedMaxDagNodes);
	}

	static FString MakeScenarioWallReplanUserText(
		const FString& OriginalUser,
		const FString& SerializedDagJson,
		const FString& StatusSummary)
	{
		return OriginalUser
			+ TEXT("\n\n---\n[Plan harness] The plan run is **stalled on scenario wall time** between steps. Emit a **compact** ")
			  TEXT("revised unreal_ai.plan_dag JSON with ONLY **new** nodes that finish remaining work (fewer, smaller steps). ")
			  TEXT("New node ids must not collide with completed `success` ids below. `dependsOn` only on success ids or new ids. ")
			  TEXT("At most ")
			+ FString::FromInt(UnrealAiWaitTime::PlannerEmittedMaxDagNodes)
			+ TEXT(" new nodes.\n\nCurrent per-node status:\n")
			+ StatusSummary + TEXT("\n\nCurrent DAG JSON:\n") + SerializedDagJson
			+ TEXT("\n\nReturn a **single JSON object** only. No markdown fences or prose outside JSON.\n---");
	}

	static FString MakeScenarioWallReplanRepairUserText(const FString& BasePrompt, const FString& ValidationErr)
	{
		return BasePrompt
			+ TEXT("\n\n---\n[Plan harness] Wall-stall replan JSON invalid.\nError: ")
			+ ValidationErr
			+ FString::Printf(
				TEXT("\nReturn one corrected JSON object; at most %d new nodes.\n---"),
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
	const UUnrealAiEditorSettings* EdSet = GetDefault<UUnrealAiEditorSettings>();
	Exec->bPlanAutoReplan = EdSet->bPlanAutoReplan && !Options.bDisableAutoReplan;
	Exec->PlanAutoReplanMaxAttempts = FMath::Clamp(EdSet->PlanAutoReplanMaxAttemptsPerRun, 0, 8);
	Exec->bUseSubagentsPolicy = FUnrealAiEditorModule::IsSubagentsEnabled();
	Exec->MaxParallelWaveNodesPolicy = Exec->bUseSubagentsPolicy ? 2 : 1;
	if (Exec->PlanAutoReplanMaxAttempts == 0)
	{
		Exec->bPlanAutoReplan = false;
	}
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HEADED_PLAN_DISABLE_AUTO_REPLAN")).TrimStartAndEnd() == TEXT("1"))
	{
		Exec->bPlanAutoReplan = false;
	}
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
		Exec->ParentSink->OnHarnessProgressLog(FString::Printf(
			TEXT("Plan policy: auto_replan=%s max_replans=%d use_subagents=%s max_parallel_wave_nodes=%d"),
			Exec->bPlanAutoReplan ? TEXT("1") : TEXT("0"),
			Exec->PlanAutoReplanMaxAttempts,
			Exec->bUseSubagentsPolicy ? TEXT("1") : TEXT("0"),
			Exec->MaxParallelWaveNodesPolicy));
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
	{
		const UUnrealAiEditorSettings* EdSet = GetDefault<UUnrealAiEditorSettings>();
		Exec->bPlanAutoReplan = EdSet->bPlanAutoReplan;
		Exec->PlanAutoReplanMaxAttempts = FMath::Clamp(EdSet->PlanAutoReplanMaxAttemptsPerRun, 0, 8);
		Exec->bUseSubagentsPolicy = FUnrealAiEditorModule::IsSubagentsEnabled();
		Exec->MaxParallelWaveNodesPolicy = Exec->bUseSubagentsPolicy ? 2 : 1;
		if (Exec->PlanAutoReplanMaxAttempts == 0)
		{
			Exec->bPlanAutoReplan = false;
		}
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HEADED_PLAN_DISABLE_AUTO_REPLAN")).TrimStartAndEnd() == TEXT("1"))
		{
			Exec->bPlanAutoReplan = false;
		}
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
		// Resume path preserves already-completed node statuses when ids still exist in the DAG.
		InContextService->ReplaceActivePlanDagWithFreshNodeResetForThread(
			InParentRequest.ProjectId,
			InParentRequest.ThreadId,
			DagJsonText,
			TSet<FString>());
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
		ContextService->SetActivePlanDagForThread(ParentRequest.ProjectId, ParentRequest.ThreadId, DagJsonText);
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
		ContextService->SetActivePlanDagForThread(ParentRequest.ProjectId, ParentRequest.ThreadId, PlannerText);
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
	const int32 WaveWidth = FMath::Max(1, MaxParallelWaveNodesPolicy);
	const int32 WaveCount = FMath::Min(WaveWidth, ReadyNodeIds.Num());
	TArray<FString> WaveNodeIds;
	for (int32 I = 0; I < WaveCount; ++I)
	{
		WaveNodeIds.Add(ReadyNodeIds[I]);
	}
	if (ParentSink.IsValid())
	{
		ParentSink->OnHarnessProgressLog(FString::Printf(
			TEXT("Ready wave: %d ready nodes (policy width=%d, use_subagents=%s). Selected wave=%s"),
			ReadyNodeIds.Num(),
			WaveWidth,
			bUseSubagentsPolicy ? TEXT("1") : TEXT("0"),
			*FString::Join(WaveNodeIds, TEXT(","))));
	}
	// Current harness executes one child turn at a time; wave computation keeps scheduling deterministic and
	// prepares a guarded path for future true parallel node dispatch.
	const FString NodeId = WaveNodeIds[0];
	const FUnrealAiDagNode* Node = Dag.Nodes.FindByPredicate([&NodeId](const FUnrealAiDagNode& N) { return N.Id == NodeId; });
	if (!Node)
	{
		Finish(false, TEXT("Ready node lookup failed."));
		return;
	}
	if (ContextService)
	{
		ContextService->SetPlanNodeStatusForThread(ParentRequest.ProjectId, ParentRequest.ThreadId, NodeId, TEXT("running"));
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
	if (ParentSink.IsValid())
	{
		const FString NodeTitle = Node->Title.IsEmpty() ? Node->Id : Node->Title;
		ParentSink->OnPlanWorkerSpanOpened(NodeId, FText::FromString(NodeTitle));
	}
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
	FString Summary;
	FString FailureCategory = TEXT("tool_or_runtime_failure");
	if (!bSuccess && !ErrorText.IsEmpty())
	{
		FString ErrOneLine = ErrorText;
		ErrOneLine.ReplaceInline(TEXT("\r"), TEXT(""));
		ErrOneLine.ReplaceInline(TEXT("\n"), TEXT(" "));
		ErrOneLine.TrimStartAndEndInline();
		if (ErrOneLine.Len() > 240)
		{
			ErrOneLine = ErrOneLine.Left(240) + TEXT("...");
		}
		FailureCategory = UnrealAiPlanExecutorPriv::MapFailureCategory(ErrOneLine);
		Summary = FString::Printf(TEXT("[%s] %s"), *FailureCategory, *ErrOneLine);
		const FString AsstTail = AssistantText.TrimStartAndEnd();
		if (!AsstTail.IsEmpty())
		{
			FString ShortAsst = AsstTail.Left(120);
			if (AsstTail.Len() > 120)
			{
				ShortAsst += TEXT("...");
			}
			Summary += TEXT(" | ");
			Summary += ShortAsst;
		}
	}
	else
	{
		Summary = AssistantText.Left(300);
	}
	if (ParentSink.IsValid())
	{
		ParentSink->OnPlanWorkerSpanClosed(NodeId, bSuccess, Summary);
	}
	if (!bSuccess)
	{
		bAnyPlanNodeFailedThisRun = true;
	}
	if (ContextService)
	{
		// Child node RunTurn uses ThreadId "<parent>_plan_<node>"; context service active session follows that thread.
		// Plan DAG readiness (GetReadyNodeIds) reads PlanNodeStatusById on the parent thread — restore it before updating status.
		ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
		ContextService->SetPlanNodeStatusForThread(
			ParentRequest.ProjectId,
			ParentRequest.ThreadId,
			NodeId,
			bSuccess ? TEXT("success") : TEXT("failed"),
			Summary);
		if (!bSuccess)
		{
			const bool bTryReplan = bPlanAutoReplan && !bHarnessPlannerOnlyNoExecute && !bAwaitingBuild
				&& PlanAutoReplanAttemptsUsed < PlanAutoReplanMaxAttempts
				&& UnrealAiPlanExecutorPriv::ShouldAutoReplanForCategory(FailureCategory);
			if (bTryReplan)
			{
				BeginNodeFailureReplanTurn(NodeId, ErrorText);
				return;
			}
			CascadeSkipDependentsAfterFailure(NodeId, ErrorText);
		}
	}
	if (ParentSink.IsValid() && !bSuccess)
	{
		ParentSink->OnHarnessProgressLog(FString::Printf(TEXT("node_failure_summary node_id=%s summary=%s"), *NodeId, *Summary.Left(240)));
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
		ContextService->SetPlanNodeStatusForThread(
			ParentRequest.ProjectId,
			ParentRequest.ThreadId,
			SkippedId,
			TEXT("skipped"),
			SkipSummary);
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

void FUnrealAiPlanExecutor::PumpGameThreadForHarnessWait(uint32 MaxWaitMs)
{
	if (MaxWaitMs == 0)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.f);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		return;
	}
	const double EndSec = FPlatformTime::Seconds() + static_cast<double>(MaxWaitMs) / 1000.0;
	while (FPlatformTime::Seconds() < EndSec)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.f);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FPlatformProcess::SleepNoStats(0.001f);
	}
}

void FUnrealAiPlanExecutor::RecomputeAnyPlanNodeFailedFromContext()
{
	bAnyPlanNodeFailedThisRun = false;
	if (!ContextService)
	{
		return;
	}
	ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
	if (const FAgentContextState* St = ContextService->GetState(ParentRequest.ProjectId, ParentRequest.ThreadId))
	{
		for (const TPair<FString, FString>& Pair : St->PlanNodeStatusById)
		{
			if (Pair.Value.Equals(TEXT("failed"), ESearchCase::IgnoreCase))
			{
				bAnyPlanNodeFailedThisRun = true;
				return;
			}
		}
	}
}

bool FUnrealAiPlanExecutor::TryApplyReplanPlannerAssistantText(const FString& PlannerText, FString& OutError)
{
	OutError.Empty();
	if (!ContextService)
	{
		OutError = TEXT("Context unavailable for replan merge.");
		return false;
	}
	ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
	FUnrealAiPlanDag NewDag;
	if (!UnrealAiPlanDag::ParseDagJson(PlannerText, NewDag, OutError))
	{
		return false;
	}
	if (!UnrealAiPlanDag::ValidateDag(NewDag, UnrealAiWaitTime::PlannerEmittedMaxDagNodes, OutError))
	{
		return false;
	}
	const FAgentContextState* St = ContextService->GetState(ParentRequest.ProjectId, ParentRequest.ThreadId);
	if (!St)
	{
		OutError = TEXT("Missing context state after LoadOrCreate.");
		return false;
	}
	FUnrealAiPlanDag Merged;
	TSet<FString> FreshIds;
	if (!UnrealAiPlanDag::MergeReplanNewNodesOntoSuccesses(Dag, St->PlanNodeStatusById, NewDag, Merged, FreshIds, OutError))
	{
		return false;
	}
	FString MergedJson;
	if (!UnrealAiPlanDag::SerializeDagJson(Merged, MergedJson, OutError))
	{
		return false;
	}
	ContextService->ReplaceActivePlanDagWithFreshNodeResetForThread(
		ParentRequest.ProjectId,
		ParentRequest.ThreadId,
		MergedJson,
		FreshIds);
	Dag = Merged;
	RecomputeAnyPlanNodeFailedFromContext();
	return true;
}

void FUnrealAiPlanExecutor::BeginNodeFailureReplanTurn(const FString& FailedNodeId, const FString& ErrorText)
{
	if (!CheckPlanWallBudgetOrFinish())
	{
		return;
	}
	if (!Harness)
	{
		ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
		CascadeSkipDependentsAfterFailure(FailedNodeId, ErrorText);
		BeginNextReadyNode();
		return;
	}
	bNodeFailureReplanRepairConsumed = false;

	ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
	FString DagJson;
	FString SerErr;
	if (!UnrealAiPlanDag::SerializeDagJson(Dag, DagJson, SerErr))
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealAi plan: node-failure replan skipped (DAG serialize failed: %s)"), *SerErr);
		CascadeSkipDependentsAfterFailure(FailedNodeId, ErrorText);
		BeginNextReadyNode();
		return;
	}
	++PlanAutoReplanAttemptsUsed;
	const FAgentContextState* St = ContextService->GetState(ParentRequest.ProjectId, ParentRequest.ThreadId);
	const FString StatusSummary = UnrealAiPlanExecutorPriv::SummarizeNodeStatusesForPlanner(St);

	FString ParentGoal = OriginalPlannerUserText.IsEmpty() ? ParentRequest.UserText : OriginalPlannerUserText;
	ParentGoal.TrimStartAndEndInline();
	{
		static constexpr int32 GGoalCap = 4000;
		if (ParentGoal.Len() > GGoalCap)
		{
			ParentGoal = ParentGoal.Left(GGoalCap) + TEXT("\n[...truncated]");
		}
	}
	PendingNodeFailureReplanBaseUserText = UnrealAiPlanExecutorPriv::MakeNodeFailureReplanUserText(
		ParentGoal,
		FailedNodeId,
		ErrorText,
		DagJson,
		StatusSummary);
	PendingNodeFailureReplanFailedNodeId = FailedNodeId;
	PendingNodeFailureReplanErrorText = ErrorText;

	FUnrealAiAgentTurnRequest PlannerReq = ParentRequest;
	PlannerReq.Mode = EUnrealAiAgentMode::Plan;
	PlannerReq.UserText = PendingNodeFailureReplanBaseUserText;

	const TSharedRef<UnrealAiPlanExecutorPriv::FCollectingSink> Sink = MakeShared<UnrealAiPlanExecutorPriv::FCollectingSink>();
	Sink->ForwardTarget = ParentSink;
	Sink->bForwardStreamToParent = true;
	const TWeakPtr<FUnrealAiPlanExecutor> WeakExec = AsShared();
	Sink->OnFinished = [WeakExec](bool bOk, const FString& Err, const FString& Txt)
	{
		if (const TSharedPtr<FUnrealAiPlanExecutor> Self = WeakExec.Pin())
		{
			Self->OnNodeFailureReplanPlannerFinished(bOk, Err, Txt);
		}
	};
	if (ParentSink.IsValid())
	{
		ParentSink->OnHarnessProgressLog(FString::Printf(
			TEXT("BeginNodeFailureReplanTurn: RunTurn (plan) for failed node %s (replan attempt %d/%d)"),
			*FailedNodeId,
			PlanAutoReplanAttemptsUsed,
			PlanAutoReplanMaxAttempts));
	}
	UE_LOG(LogTemp, Display, TEXT("UnrealAi plan: node-failure replan HTTP (failed=%s)"), *FailedNodeId);
	Harness->RunTurn(PlannerReq, Sink);
}

void FUnrealAiPlanExecutor::OnNodeFailureReplanPlannerFinished(bool bSuccess, const FString& ErrorText, const FString& PlannerText)
{
	if (ParentSink.IsValid())
	{
		ParentSink->OnHarnessProgressLog(FString::Printf(
			TEXT("OnNodeFailureReplanPlannerFinished: success=%s err_len=%d text_len=%d"),
			bSuccess ? TEXT("1") : TEXT("0"),
			ErrorText.Len(),
			PlannerText.Len()));
	}
	if (bCancelled)
	{
		Finish(false, TEXT("Plan cancelled."));
		return;
	}
	const FString FailedNode = PendingNodeFailureReplanFailedNodeId;
	const FString FailedErr = PendingNodeFailureReplanErrorText;

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealAi plan: node-failure replan harness failed: %s"), *ErrorText);
		if (ContextService && !FailedNode.IsEmpty())
		{
			ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
			CascadeSkipDependentsAfterFailure(FailedNode, ErrorText.IsEmpty() ? FailedErr : ErrorText);
		}
		if (ParentSink.IsValid())
		{
			ParentSink->OnPlanHarnessSubTurnComplete();
		}
		BeginNextReadyNode();
		PendingNodeFailureReplanBaseUserText.Reset();
		PendingNodeFailureReplanFailedNodeId.Reset();
		PendingNodeFailureReplanErrorText.Reset();
		return;
	}

	FString ParseOrMergeErr;
	if (!TryApplyReplanPlannerAssistantText(PlannerText, ParseOrMergeErr))
	{
		if (!bNodeFailureReplanRepairConsumed)
		{
			bNodeFailureReplanRepairConsumed = true;
			FUnrealAiAgentTurnRequest PlannerReq = ParentRequest;
			PlannerReq.Mode = EUnrealAiAgentMode::Plan;
			PlannerReq.UserText = UnrealAiPlanExecutorPriv::MakeNodeFailureReplanRepairUserText(
				PendingNodeFailureReplanBaseUserText,
				ParseOrMergeErr);
			PendingNodeFailureReplanBaseUserText.Reset();
			const TSharedRef<UnrealAiPlanExecutorPriv::FCollectingSink> Sink = MakeShared<UnrealAiPlanExecutorPriv::FCollectingSink>();
			Sink->ForwardTarget = ParentSink;
			Sink->bForwardStreamToParent = true;
			const TWeakPtr<FUnrealAiPlanExecutor> WeakExec = AsShared();
			Sink->OnFinished = [WeakExec](bool bOk, const FString& Err, const FString& Txt)
			{
				if (const TSharedPtr<FUnrealAiPlanExecutor> Self = WeakExec.Pin())
				{
					Self->OnNodeFailureReplanPlannerFinished(bOk, Err, Txt);
				}
			};
			if (ParentSink.IsValid())
			{
				ParentSink->OnHarnessProgressLog(TEXT("OnNodeFailureReplanPlannerFinished: repair replan pass (invalid JSON/merge)"));
			}
			Harness->RunTurn(PlannerReq, Sink);
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("UnrealAi plan: node-failure replan merge failed after repair: %s"), *ParseOrMergeErr);
		if (ContextService && !FailedNode.IsEmpty())
		{
			ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
			CascadeSkipDependentsAfterFailure(FailedNode, ParseOrMergeErr);
		}
		if (ParentSink.IsValid())
		{
			ParentSink->OnPlanHarnessSubTurnComplete();
		}
		BeginNextReadyNode();
		PendingNodeFailureReplanBaseUserText.Reset();
		PendingNodeFailureReplanFailedNodeId.Reset();
		PendingNodeFailureReplanErrorText.Reset();
		return;
	}

	PendingNodeFailureReplanBaseUserText.Reset();
	PendingNodeFailureReplanFailedNodeId.Reset();
	PendingNodeFailureReplanErrorText.Reset();
	bAnyPlanNodeFailedThisRun = false;
	RecomputeAnyPlanNodeFailedFromContext();
	if (ParentSink.IsValid())
	{
		ParentSink->OnHarnessProgressLog(TEXT("OnNodeFailureReplanPlannerFinished: replan merged; resuming DAG execution"));
		ParentSink->OnPlanHarnessSubTurnComplete();
	}
	UE_LOG(LogTemp, Display, TEXT("UnrealAi plan: node-failure replan merged OK → BeginNextReadyNode"));
	BeginNextReadyNode();
}

bool FUnrealAiPlanExecutor::TryScenarioWallCompactReplanForHeadedHarness(
	const TSharedPtr<IAgentRunSink>& HarnessSink,
	double& InOutWallDeadlineSec,
	uint32 ScenarioWallMs)
{
	if (!bRunning || !bPlanAutoReplan || bScenarioWallReplanConsumed || bHarnessPlannerOnlyNoExecute || bAwaitingBuild)
	{
		return false;
	}
	if (PlanAutoReplanAttemptsUsed >= PlanAutoReplanMaxAttempts)
	{
		return false;
	}
	if (!Harness || Harness->IsTurnInProgress())
	{
		return false;
	}
	if (!ContextService)
	{
		return false;
	}
	FString DagJson;
	FString SerErr;
	if (!UnrealAiPlanDag::SerializeDagJson(Dag, DagJson, SerErr))
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealAi plan: wall replan skipped (serialize DAG: %s)"), *SerErr);
		return false;
	}

	bScenarioWallReplanConsumed = true;
	++PlanAutoReplanAttemptsUsed;

	ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
	const FAgentContextState* St = ContextService->GetState(ParentRequest.ProjectId, ParentRequest.ThreadId);
	const FString StatusSummary = UnrealAiPlanExecutorPriv::SummarizeNodeStatusesForPlanner(St);

	FString ParentGoal = OriginalPlannerUserText.IsEmpty() ? ParentRequest.UserText : OriginalPlannerUserText;
	ParentGoal.TrimStartAndEndInline();
	{
		static constexpr int32 GGoalCap = 4000;
		if (ParentGoal.Len() > GGoalCap)
		{
			ParentGoal = ParentGoal.Left(GGoalCap) + TEXT("\n[...truncated]");
		}
	}
	const FString BaseWallText =
		UnrealAiPlanExecutorPriv::MakeScenarioWallReplanUserText(ParentGoal, DagJson, StatusSummary);

	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
	struct FWallReplanWait
	{
		bool bHarnessOk = false;
		FString HarnessErr;
		FString AssistantText;
		FEvent* Ev = nullptr;
	} Wait{};
	Wait.Ev = DoneEvent;

	auto RunWallPlannerSync = [&](const FString& UserText) -> void
	{
		Wait.bHarnessOk = false;
		Wait.HarnessErr.Reset();
		Wait.AssistantText.Reset();
		DoneEvent->Reset();
		FUnrealAiAgentTurnRequest PlannerReq = ParentRequest;
		PlannerReq.Mode = EUnrealAiAgentMode::Plan;
		PlannerReq.UserText = UserText;
		const TSharedRef<UnrealAiPlanExecutorPriv::FCollectingSink> Sink = MakeShared<UnrealAiPlanExecutorPriv::FCollectingSink>();
		Sink->ForwardTarget = HarnessSink;
		Sink->bForwardStreamToParent = HarnessSink.IsValid();
		Sink->OnFinished = [&Wait](bool bOk, const FString& Err, const FString& Txt)
		{
			Wait.bHarnessOk = bOk;
			Wait.HarnessErr = Err;
			Wait.AssistantText = Txt;
			if (Wait.Ev)
			{
				Wait.Ev->Trigger();
			}
		};
		Harness->RunTurn(PlannerReq, Sink);
		const double Deadline =
			FPlatformTime::Seconds() + static_cast<double>(UnrealAiWaitTime::HarnessPlanReplanSyncMaxMs) / 1000.0;
		while (FPlatformTime::Seconds() < Deadline)
		{
			if (DoneEvent->Wait(0u))
			{
				break;
			}
			FHttpModule::Get().GetHttpManager().Tick(0.f);
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::SleepNoStats(0.001f);
		}
		if (!DoneEvent->Wait(0u) && Harness->IsTurnInProgress())
		{
			UE_LOG(LogTemp, Warning, TEXT("UnrealAi plan: wall replan planner timed out; CancelTurn."));
			Harness->CancelTurn();
			PumpGameThreadForHarnessWait(UnrealAiWaitTime::HarnessCancelDrainWaitMs);
		}
	};

	bool bMerged = false;
	FString LastErr;
	FString AttemptUser = BaseWallText;
	for (int32 Attempt = 0; Attempt < 2; ++Attempt)
	{
		if (Attempt > 0)
		{
			AttemptUser = UnrealAiPlanExecutorPriv::MakeScenarioWallReplanRepairUserText(BaseWallText, LastErr);
		}
		RunWallPlannerSync(AttemptUser);
		if (!Wait.bHarnessOk)
		{
			LastErr = Wait.HarnessErr.IsEmpty() ? TEXT("planner harness failed") : Wait.HarnessErr;
			continue;
		}
		FString MergeErr;
		if (TryApplyReplanPlannerAssistantText(Wait.AssistantText, MergeErr))
		{
			bMerged = true;
			break;
		}
		LastErr = MergeErr;
	}

	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
	DoneEvent = nullptr;
	Wait.Ev = nullptr;

	if (!bMerged)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealAi plan: wall compact replan did not merge (%s)"), *LastErr);
		return false;
	}

	InOutWallDeadlineSec = FPlatformTime::Seconds() + static_cast<double>(ScenarioWallMs) / 1000.0;
	if (HarnessSink.IsValid())
	{
		HarnessSink->OnHarnessProgressLog(
			TEXT("TryScenarioWallCompactReplanForHeadedHarness: merged OK; scenario wall deadline extended from now"));
	}
	BeginNextReadyNode();
	return true;
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
	if (ContextService)
	{
		ContextService->SaveNow(ParentRequest.ProjectId, ParentRequest.ThreadId);
	}
	if (ParentSink.IsValid())
	{
		ParentSink->OnRunFinished(bSuccess, ErrorText);
	}
}
