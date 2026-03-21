#include "Harness/FUnrealAiAgentHarness.h"

#include "Async/Async.h"
#include "Templates/SharedPointer.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Harness/FUnrealAiConversationStore.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/ILlmTransport.h"
#include "Harness/UnrealAiTurnLlmRequestBuilder.h"
#include "Harness/IToolExecutionHost.h"
#include "Context/IAgentContextService.h"
#include "Context/AgentContextTypes.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Harness/UnrealAiLlmInvocationService.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <atomic>

namespace UnrealAiAgentHarnessPriv
{
	struct FAgentTurnRunner : public TSharedFromThis<FAgentTurnRunner>
	{
		FUnrealAiAgentTurnRequest Request;
		TSharedPtr<IAgentRunSink> Sink;
		IUnrealAiPersistence* Persistence = nullptr;
		IAgentContextService* ContextService = nullptr;
		FUnrealAiModelProfileRegistry* Profiles = nullptr;
		FUnrealAiToolCatalog* Catalog = nullptr;
		TSharedPtr<ILlmTransport> Transport;
		IToolExecutionHost* Tools = nullptr;
		TUniquePtr<FUnrealAiConversationStore> Conv;

		FGuid RunId;
		int32 LlmRound = 0;
		static constexpr int32 MaxLlmRounds = 16;

		FString AssistantBuffer;
		TArray<FUnrealAiToolCallSpec> PendingToolCalls;
		bool bFinishSeen = false;
		std::atomic<bool> bCancelled{false};
		std::atomic<bool> bTerminal{false};

		void HandleEvent(const FUnrealAiLlmStreamEvent& Ev);
		void DispatchLlm();
		void CompleteToolPath();
		void CompleteAssistantOnly();
		void Fail(const FString& Msg);
		void Succeed();

		static constexpr int32 CharPerTokenApprox = 4;
	};

	void FAgentTurnRunner::Fail(const FString& Msg)
	{
		bool Expected = false;
		if (!bTerminal.compare_exchange_strong(Expected, true, std::memory_order_relaxed))
		{
			return;
		}
		if (Sink.IsValid())
		{
			Sink->OnRunFinished(false, Msg);
		}
	}

	void FAgentTurnRunner::Succeed()
	{
		bool Expected = false;
		if (!bTerminal.compare_exchange_strong(Expected, true, std::memory_order_relaxed))
		{
			return;
		}
		if (Conv.IsValid())
		{
			Conv->SaveNow();
		}
		if (Request.bRecordAssistantAsStubToolResult && ContextService && Conv.IsValid())
		{
			FString LastAssistant;
			for (int32 i = Conv->GetMessages().Num() - 1; i >= 0; --i)
			{
				if (Conv->GetMessages()[i].Role == TEXT("assistant") && Conv->GetMessages()[i].ToolCalls.Num() == 0)
				{
					LastAssistant = Conv->GetMessages()[i].Content;
					break;
				}
			}
			if (!LastAssistant.IsEmpty())
			{
				ContextService->LoadOrCreate(Request.ProjectId, Request.ThreadId);
				FContextRecordPolicy Policy;
				ContextService->RecordToolResult(FName(TEXT("assistant_harness")), LastAssistant, Policy);
				ContextService->SaveNow(Request.ProjectId, Request.ThreadId);
			}
		}
		if (Sink.IsValid())
		{
			Sink->OnRunFinished(true, FString());
		}
	}

	void FAgentTurnRunner::DispatchLlm()
	{
		if (!Transport.IsValid() || !Profiles || !Catalog || !ContextService || !Conv.IsValid() || !Tools)
		{
			Fail(TEXT("Harness not configured"));
			return;
		}
		if (bCancelled.load(std::memory_order_relaxed))
		{
			Fail(TEXT("Cancelled"));
			return;
		}
		if (LlmRound >= MaxLlmRounds)
		{
			Fail(TEXT("Max tool/LLM rounds exceeded"));
			return;
		}
		++LlmRound;
		if (LlmRound > 1 && Sink.IsValid())
		{
			Sink->OnRunContinuation(LlmRound - 1, MaxLlmRounds);
		}
		AssistantBuffer.Reset();
		PendingToolCalls.Reset();
		bFinishSeen = false;

		FUnrealAiLlmRequest LlmReq;
		FString BuildErr;
		if (!UnrealAiTurnLlmRequestBuilder::Build(
				Request,
				LlmRound,
				MaxLlmRounds,
				ContextService,
				Profiles,
				Catalog,
				Conv.Get(),
				CharPerTokenApprox,
				LlmReq,
				BuildErr))
		{
			Fail(BuildErr);
			return;
		}

		const TSharedPtr<FAgentTurnRunner> Self = AsShared();
		const FUnrealAiLlmInvocationService Invoker(Transport);
		Invoker.SubmitStreamChatCompletion(
			LlmReq,
			FUnrealAiLlmStreamCallback::CreateLambda(
				[Self](const FUnrealAiLlmStreamEvent& Ev)
				{
					AsyncTask(ENamedThreads::GameThread,
							  [Self, Ev]()
							  {
								  Self->HandleEvent(Ev);
							  });
				}));
	}

	void FAgentTurnRunner::HandleEvent(const FUnrealAiLlmStreamEvent& Ev)
	{
		if (bCancelled.load(std::memory_order_relaxed) || bTerminal.load(std::memory_order_relaxed))
		{
			return;
		}
		if (!Sink.IsValid())
		{
			return;
		}
		switch (Ev.Type)
		{
		case EUnrealAiLlmStreamEventType::AssistantDelta:
			AssistantBuffer += Ev.DeltaText;
			Sink->OnAssistantDelta(Ev.DeltaText);
			break;
		case EUnrealAiLlmStreamEventType::ThinkingDelta:
			Sink->OnThinkingDelta(Ev.DeltaText);
			break;
		case EUnrealAiLlmStreamEventType::ToolCalls:
			PendingToolCalls.Append(Ev.ToolCalls);
			break;
		case EUnrealAiLlmStreamEventType::Finish:
			bFinishSeen = true;
			if (Ev.FinishReason == TEXT("tool_calls") && PendingToolCalls.Num() == 0)
			{
				Fail(TEXT("Model requested tools but sent no tool_calls"));
				break;
			}
			if (Ev.FinishReason == TEXT("tool_calls") || PendingToolCalls.Num() > 0)
			{
				CompleteToolPath();
			}
			else
			{
				CompleteAssistantOnly();
			}
			break;
		case EUnrealAiLlmStreamEventType::Error:
			Fail(Ev.ErrorMessage.IsEmpty() ? TEXT("LLM error") : Ev.ErrorMessage);
			break;
		default:
			break;
		}
	}

	void FAgentTurnRunner::CompleteToolPath()
	{
		if (!Tools || !Conv.IsValid())
		{
			Fail(TEXT("Tool host missing"));
			return;
		}
		FUnrealAiConversationMessage Am;
		Am.Role = TEXT("assistant");
		Am.Content = AssistantBuffer;
		Am.ToolCalls = PendingToolCalls;
		Conv->GetMessagesMutable().Add(Am);

		for (const FUnrealAiToolCallSpec& Tc : PendingToolCalls)
		{
			if (Sink.IsValid())
			{
				Sink->OnToolCallStarted(Tc.Name, Tc.Id, Tc.ArgumentsJson);
			}
			const FUnrealAiToolInvocationResult Inv = Tools->InvokeTool(Tc.Name, Tc.ArgumentsJson, Tc.Id);
			if (Sink.IsValid())
			{
				const FString Preview = Inv.bOk ? Inv.ContentForModel : Inv.ErrorMessage;
				Sink->OnToolCallFinished(Tc.Name, Tc.Id, Inv.bOk, Preview);
			}
			if (Tc.Name == TEXT("agent_emit_todo_plan") && Sink.IsValid())
			{
				FString PlanTitle = TEXT("Plan");
				TSharedPtr<FJsonObject> ArgsObj;
				TSharedRef<TJsonReader<>> AR = TJsonReaderFactory<>::Create(Tc.ArgumentsJson);
				if (FJsonSerializer::Deserialize(AR, ArgsObj) && ArgsObj.IsValid())
				{
					ArgsObj->TryGetStringField(TEXT("title"), PlanTitle);
				}
				const FString PlanBody = (Inv.bOk && Inv.ContentForModel.TrimStart().StartsWith(TEXT("{")))
					? Inv.ContentForModel
					: Tc.ArgumentsJson;
				Sink->OnTodoPlanEmitted(PlanTitle, PlanBody);
			}
			FUnrealAiConversationMessage Tm;
			Tm.Role = TEXT("tool");
			Tm.ToolCallId = Tc.Id;
			Tm.Content = Inv.bOk ? Inv.ContentForModel : Inv.ErrorMessage;
			Conv->GetMessagesMutable().Add(Tm);
		}
		PendingToolCalls.Reset();
		AssistantBuffer.Reset();
		DispatchLlm();
	}

	void FAgentTurnRunner::CompleteAssistantOnly()
	{
		if (!Conv.IsValid())
		{
			Fail(TEXT("Conversation missing"));
			return;
		}
		FUnrealAiConversationMessage Am;
		Am.Role = TEXT("assistant");
		Am.Content = AssistantBuffer;
		Conv->GetMessagesMutable().Add(Am);
		Succeed();
	}
}

FUnrealAiAgentHarness::FUnrealAiAgentHarness(
	IUnrealAiPersistence* InPersistence,
	IAgentContextService* InContext,
	FUnrealAiModelProfileRegistry* InProfiles,
	FUnrealAiToolCatalog* InCatalog,
	TSharedPtr<ILlmTransport> InTransport,
	IToolExecutionHost* InToolHost)
	: Persistence(InPersistence)
	, Context(InContext)
	, Profiles(InProfiles)
	, Catalog(InCatalog)
	, Transport(MoveTemp(InTransport))
	, ToolHost(InToolHost)
{
}

FUnrealAiAgentHarness::~FUnrealAiAgentHarness() = default;

void FUnrealAiAgentHarness::CancelTurn()
{
	if (ActiveRunner.IsValid())
	{
		ActiveRunner->bCancelled.store(true, std::memory_order_relaxed);
	}
	if (Transport.IsValid())
	{
		Transport->CancelActiveRequest();
	}
}

bool FUnrealAiAgentHarness::IsTurnInProgress() const
{
	if (!ActiveRunner.IsValid())
	{
		return false;
	}
	return !ActiveRunner->bTerminal.load(std::memory_order_relaxed)
		&& !ActiveRunner->bCancelled.load(std::memory_order_relaxed);
}

void FUnrealAiAgentHarness::RunTurn(const FUnrealAiAgentTurnRequest& Request, TSharedPtr<IAgentRunSink> Sink)
{
	CancelTurn();
	if (!Sink.IsValid() || !Persistence || !Context || !Profiles || !Catalog || !Transport.IsValid() || !ToolHost)
	{
		if (Sink.IsValid())
		{
			Sink->OnRunFinished(false, TEXT("Harness not fully configured"));
		}
		return;
	}

	const TSharedPtr<UnrealAiAgentHarnessPriv::FAgentTurnRunner> Runner = MakeShared<UnrealAiAgentHarnessPriv::FAgentTurnRunner>();
	ActiveRunner = Runner;
	Runner->Request = Request;
	Runner->Sink = Sink;
	Runner->Persistence = Persistence;
	Runner->ContextService = Context;
	Runner->Profiles = Profiles;
	Runner->Catalog = Catalog;
	Runner->Transport = Transport;
	Runner->Tools = ToolHost;
	Runner->Conv = MakeUnique<FUnrealAiConversationStore>(Persistence);
	Runner->Conv->LoadOrCreate(Request.ProjectId, Request.ThreadId);

	Context->LoadOrCreate(Request.ProjectId, Request.ThreadId);
	ToolHost->SetToolSession(Request.ProjectId, Request.ThreadId);

	Runner->RunId = FGuid::NewGuid();
	FUnrealAiRunIds Ids;
	Ids.RunId = Runner->RunId;
	Sink->OnRunStarted(Ids);

	FUnrealAiConversationMessage UserMsg;
	UserMsg.Role = TEXT("user");
	UserMsg.Content = Request.UserText;
	Runner->Conv->GetMessagesMutable().Add(UserMsg);

	Runner->DispatchLlm();
}
