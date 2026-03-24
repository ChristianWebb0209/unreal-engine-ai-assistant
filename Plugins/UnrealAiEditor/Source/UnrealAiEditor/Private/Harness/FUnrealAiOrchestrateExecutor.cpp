#include "Harness/FUnrealAiOrchestrateExecutor.h"

#include "Context/IAgentContextService.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/IUnrealAiAgentHarness.h"

namespace UnrealAiOrchestrateExecutorPriv
{
	class FCollectingSink final : public IAgentRunSink
	{
	public:
		TFunction<void(bool, const FString&, const FString&)> OnFinished;
		FString AssistantText;

		virtual void OnRunStarted(const FUnrealAiRunIds& Ids) override { (void)Ids; }
		virtual void OnAssistantDelta(const FString& Chunk) override { AssistantText += Chunk; }
		virtual void OnThinkingDelta(const FString& Chunk) override { (void)Chunk; }
		virtual void OnToolCallStarted(const FString& ToolName, const FString& CallId, const FString& ArgumentsJson) override
		{
			(void)ToolName;
			(void)CallId;
			(void)ArgumentsJson;
		}
		virtual void OnToolCallFinished(
			const FString& ToolName,
			const FString& CallId,
			bool bSuccess,
			const FString& ResultPreview,
			const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation) override
		{
			(void)ToolName;
			(void)CallId;
			(void)bSuccess;
			(void)ResultPreview;
			(void)EditorPresentation;
		}
		virtual void OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint) override
		{
			(void)PhaseIndex;
			(void)TotalPhasesHint;
		}
		virtual void OnTodoPlanEmitted(const FString& Title, const FString& PlanJson) override
		{
			(void)Title;
			(void)PlanJson;
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

TSharedRef<FUnrealAiOrchestrateExecutor> FUnrealAiOrchestrateExecutor::Start(
	IUnrealAiAgentHarness* InHarness,
	IAgentContextService* InContextService,
	const FUnrealAiAgentTurnRequest& InParentRequest,
	TSharedPtr<IAgentRunSink> InParentSink)
{
	TSharedRef<FUnrealAiOrchestrateExecutor> Exec = MakeShared<FUnrealAiOrchestrateExecutor>();
	Exec->Harness = InHarness;
	Exec->ContextService = InContextService;
	Exec->ParentRequest = InParentRequest;
	Exec->ParentSink = MoveTemp(InParentSink);
	Exec->bRunning = true;
	Exec->ParentIds.RunId = FGuid::NewGuid();
	Exec->ParentIds.ParentRunId.Invalidate();
	Exec->ParentIds.WorkerIndex = INDEX_NONE;
	if (Exec->ParentSink.IsValid())
	{
		Exec->ParentSink->OnRunStarted(Exec->ParentIds);
	}
	Exec->BeginPlannerTurn();
	return Exec;
}

void FUnrealAiOrchestrateExecutor::Cancel()
{
	bCancelled = true;
	if (Harness)
	{
		Harness->CancelTurn();
	}
}

void FUnrealAiOrchestrateExecutor::BeginPlannerTurn()
{
	if (!Harness)
	{
		Finish(false, TEXT("Harness unavailable for orchestrate planner."));
		return;
	}
	FUnrealAiAgentTurnRequest PlannerReq = ParentRequest;
	PlannerReq.Mode = EUnrealAiAgentMode::Orchestrate;
	const TSharedRef<UnrealAiOrchestrateExecutorPriv::FCollectingSink> Sink = MakeShared<UnrealAiOrchestrateExecutorPriv::FCollectingSink>();
	const TWeakPtr<FUnrealAiOrchestrateExecutor> WeakExec = AsShared();
	Sink->OnFinished = [WeakExec](bool bSuccess, const FString& ErrorText, const FString& Text)
	{
		if (const TSharedPtr<FUnrealAiOrchestrateExecutor> Self = WeakExec.Pin())
		{
			Self->OnPlannerFinished(bSuccess, ErrorText, Text);
		}
	};
	Harness->RunTurn(PlannerReq, Sink);
}

void FUnrealAiOrchestrateExecutor::OnPlannerFinished(bool bSuccess, const FString& ErrorText, const FString& PlannerText)
{
	if (bCancelled)
	{
		Finish(false, TEXT("Orchestrate cancelled."));
		return;
	}

	// Chat naming token propagation:
	// Orchestrate planner output is not streamed directly into the chat transcript (we only forward
	// a short "plan ready" message). If the model appended `<chat-name: ...>` to the planner response,
	// forward just that token to the parent sink so the UI can rename/strip it.
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
	if (!UnrealAiOrchestrateDag::ParseDagJson(PlannerText, Dag, ParseError))
	{
		Finish(false, FString::Printf(TEXT("Planner returned invalid DAG JSON: %s"), *ParseError));
		return;
	}
	if (!UnrealAiOrchestrateDag::ValidateDag(Dag, 64, ParseError))
	{
		Finish(false, FString::Printf(TEXT("Planner DAG failed validation: %s"), *ParseError));
		return;
	}
	if (ContextService)
	{
		ContextService->LoadOrCreate(ParentRequest.ProjectId, ParentRequest.ThreadId);
		ContextService->SetActiveOrchestrateDag(PlannerText);
	}
	if (ParentSink.IsValid())
	{
		ParentSink->OnAssistantDelta(FString::Printf(TEXT("Orchestrate plan ready: %d nodes. Executing serially.\n"), Dag.Nodes.Num()));
	}
	BeginNextReadyNode();
}

void FUnrealAiOrchestrateExecutor::BeginNextReadyNode()
{
	if (bCancelled)
	{
		Finish(false, TEXT("Orchestrate cancelled."));
		return;
	}
	const FAgentContextState* State = nullptr;
	TMap<FString, FString> Statuses;
	if (ContextService)
	{
		State = ContextService->GetState(ParentRequest.ProjectId, ParentRequest.ThreadId);
		if (State)
		{
			Statuses = State->OrchestrateNodeStatusById;
		}
	}
	UnrealAiOrchestrateDag::GetReadyNodeIds(Dag, Statuses, ReadyNodeIds);
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
		ContextService->SetOrchestrateNodeStatus(NodeId, TEXT("running"));
	}

	FUnrealAiAgentTurnRequest ChildReq = ParentRequest;
	ChildReq.Mode = EUnrealAiAgentMode::Agent;
	ChildReq.ThreadId = FString::Printf(TEXT("%s_orch_%s"), *ParentRequest.ThreadId, *NodeId);
	ChildReq.UserText = FString::Printf(
		TEXT("Execute orchestrate node '%s'.\nTitle: %s\nHint: %s"),
		*Node->Id,
		Node->Title.IsEmpty() ? *Node->Id : *Node->Title,
		*Node->Hint);

	const TSharedRef<UnrealAiOrchestrateExecutorPriv::FCollectingSink> Sink = MakeShared<UnrealAiOrchestrateExecutorPriv::FCollectingSink>();
	const TWeakPtr<FUnrealAiOrchestrateExecutor> WeakExec = AsShared();
	Sink->OnFinished = [WeakExec, NodeId](bool bSuccess, const FString& Error, const FString& AssistantText)
	{
		if (const TSharedPtr<FUnrealAiOrchestrateExecutor> Self = WeakExec.Pin())
		{
			Self->OnNodeFinished(NodeId, bSuccess, Error, AssistantText);
		}
	};
	Harness->RunTurn(ChildReq, Sink);
}

void FUnrealAiOrchestrateExecutor::OnNodeFinished(const FString& NodeId, bool bSuccess, const FString& ErrorText, const FString& AssistantText)
{
	const FString Summary = AssistantText.Left(300);
	if (ContextService)
	{
		ContextService->SetOrchestrateNodeStatus(NodeId, bSuccess ? TEXT("success") : TEXT("failed"), Summary);
	}
	if (ParentSink.IsValid())
	{
		if (bSuccess)
		{
			ParentSink->OnAssistantDelta(FString::Printf(TEXT("[orchestrate] %s completed.\n"), *NodeId));
		}
		else
		{
			ParentSink->OnAssistantDelta(FString::Printf(TEXT("[orchestrate] %s failed: %s\n"), *NodeId, *ErrorText));
		}
	}
	BeginNextReadyNode();
}

void FUnrealAiOrchestrateExecutor::Finish(bool bSuccess, const FString& ErrorText)
{
	if (!bRunning)
	{
		return;
	}
	bRunning = false;
	if (ParentSink.IsValid())
	{
		ParentSink->OnRunFinished(bSuccess, ErrorText);
	}
}

