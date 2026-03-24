#include "Harness/FUnrealAiWorkerOrchestrator.h"

#include "Templates/SharedPointer.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/IUnrealAiAgentHarness.h"

namespace UnrealAiWorkerOrchestratorPriv
{
	class FChainSink final : public IAgentRunSink
	{
	public:
		TSharedPtr<IAgentRunSink> Parent;
		FUnrealAiRunIds WorkerIds;
		FString AccumulatedText;
		TFunction<void(bool bOk, const FString& Err)> OnWorkerComplete;

		virtual void OnRunStarted(const FUnrealAiRunIds& Ids) override
		{
			if (Parent.IsValid())
			{
				Parent->OnRunStarted(WorkerIds);
			}
			(void)Ids;
		}

		virtual void OnContextUserMessages(const TArray<FString>& Messages) override
		{
			if (Parent.IsValid())
			{
				Parent->OnContextUserMessages(Messages);
			}
		}

		virtual void OnAssistantDelta(const FString& Chunk) override
		{
			AccumulatedText += Chunk;
			if (Parent.IsValid())
			{
				Parent->OnAssistantDelta(Chunk);
			}
		}

		virtual void OnThinkingDelta(const FString& Chunk) override
		{
			if (Parent.IsValid())
			{
				Parent->OnThinkingDelta(Chunk);
			}
		}

		virtual void OnToolCallStarted(const FString& ToolName, const FString& CallId, const FString& ArgumentsJson) override
		{
			if (Parent.IsValid())
			{
				Parent->OnToolCallStarted(ToolName, CallId, ArgumentsJson);
			}
		}

		virtual void OnToolCallFinished(
			const FString& ToolName,
			const FString& CallId,
			bool bSuccess,
			const FString& ResultPreview,
			const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation) override
		{
			if (Parent.IsValid())
			{
				Parent->OnToolCallFinished(ToolName, CallId, bSuccess, ResultPreview, EditorPresentation);
			}
		}

		virtual void OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint) override
		{
			if (Parent.IsValid())
			{
				Parent->OnRunContinuation(PhaseIndex, TotalPhasesHint);
			}
		}

		virtual void OnTodoPlanEmitted(const FString& Title, const FString& PlanJson) override
		{
			if (Parent.IsValid())
			{
				Parent->OnTodoPlanEmitted(Title, PlanJson);
			}
		}

		virtual void OnRunFinished(bool bSuccess, const FString& ErrorMessage) override
		{
			if (OnWorkerComplete)
			{
				OnWorkerComplete(bSuccess, ErrorMessage);
			}
		}
	};

	struct FChainState : public TSharedFromThis<FChainState>
	{
		IUnrealAiAgentHarness* Harness = nullptr;
		FUnrealAiAgentTurnRequest Template;
		TArray<FString> Goals;
		int32 MaxN = 0;
		int32 Index = 0;
		FGuid ParentRunId;
		TSharedPtr<IAgentRunSink> Parent;
		TArray<FUnrealAiWorkerResult> Gathered;

		void KickNext();
	};

	void FChainState::KickNext()
	{
		if (!Harness)
		{
			return;
		}
		if (Index >= MaxN)
		{
			const FUnrealAiWorkerResult Merged = FUnrealAiWorkerOrchestrator::MergeDeterministic(Gathered);
			if (Parent.IsValid())
			{
				if (!Merged.Summary.IsEmpty())
				{
					Parent->OnAssistantDelta(FString::Printf(TEXT("\n\n[Merged %d workers]\n%s"), MaxN, *Merged.Summary));
				}
				Parent->OnRunFinished(
					Merged.Status != TEXT("failed"),
					Merged.Errors.Num() > 0 ? FString::Join(Merged.Errors, TEXT("; ")) : FString());
			}
			return;
		}

		TSharedRef<FChainSink> Sink = MakeShared<FChainSink>();
		Sink->Parent = Parent;
		Sink->WorkerIds.RunId = FGuid::NewGuid();
		Sink->WorkerIds.ParentRunId = ParentRunId;
		Sink->WorkerIds.WorkerIndex = Index;

		const TSharedPtr<FChainState> Self = AsShared();
		const int32 WorkerIdx = Index;
		Sink->OnWorkerComplete = [Self, Sink, WorkerIdx](bool bOk, const FString& Err)
		{
			FUnrealAiWorkerResult One;
			One.Status = bOk ? TEXT("success") : TEXT("failed");
			One.Summary = Sink->AccumulatedText;
			if (!bOk)
			{
				One.Errors.Add(Err);
			}
			Self->Gathered.Add(MoveTemp(One));
			Self->Index = WorkerIdx + 1;
			Self->KickNext();
		};

		FUnrealAiAgentTurnRequest Wreq = Template;
		Wreq.UserText = Goals[Index];
		Wreq.ThreadId = Template.ThreadId + FString::Printf(TEXT("_worker_%d"), Index);

		Harness->RunTurn(Wreq, Sink);
	}
}

FUnrealAiWorkerResult FUnrealAiWorkerOrchestrator::MergeDeterministic(const TArray<FUnrealAiWorkerResult>& Workers)
{
	FUnrealAiWorkerResult Merged;
	Merged.Status = TEXT("success");
	for (const FUnrealAiWorkerResult& W : Workers)
	{
		if (W.Status == TEXT("failed"))
		{
			Merged.Status = TEXT("partial");
		}
		if (!W.Summary.IsEmpty())
		{
			if (!Merged.Summary.IsEmpty())
			{
				Merged.Summary += TEXT("\n---\n");
			}
			Merged.Summary += W.Summary;
		}
		Merged.Artifacts.Append(W.Artifacts);
		Merged.Errors.Append(W.Errors);
		Merged.FollowUpQuestions.Append(W.FollowUpQuestions);
	}
	return Merged;
}

void FUnrealAiWorkerOrchestrator::RunSequentialWorkers(
	IUnrealAiAgentHarness& Harness,
	const TArray<FString>& WorkerGoals,
	const FUnrealAiAgentTurnRequest& Template,
	TSharedPtr<IAgentRunSink> ParentSink,
	int32 MaxParallelismCap)
{
	if (WorkerGoals.Num() == 0 || !ParentSink.IsValid())
	{
		return;
	}

	const int32 N = FMath::Min(WorkerGoals.Num(), FMath::Max(1, MaxParallelismCap));
	const TSharedRef<UnrealAiWorkerOrchestratorPriv::FChainState> S = MakeShared<UnrealAiWorkerOrchestratorPriv::FChainState>();
	S->Harness = &Harness;
	S->Template = Template;
	S->Goals.Reserve(N);
	for (int32 Gi = 0; Gi < N; ++Gi)
	{
		S->Goals.Add(WorkerGoals[Gi]);
	}
	S->MaxN = N;
	S->Index = 0;
	S->ParentRunId = FGuid::NewGuid();
	S->Parent = ParentSink;

	FUnrealAiRunIds ParentIds;
	ParentIds.RunId = S->ParentRunId;
	ParentIds.WorkerIndex = -1;
	ParentSink->OnRunStarted(ParentIds);

	S->KickNext();
}
