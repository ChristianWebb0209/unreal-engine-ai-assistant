#include "Harness/FUnrealAiAgentHarness.h"

#include "Async/Async.h"
#include "Harness/UnrealAiAgentTypes.h"
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
#include "Backend/FUnrealAiUsageTracker.h"
#include "Backend/IUnrealAiPersistence.h"
#include "HAL/PlatformMisc.h"
#include "Harness/UnrealAiLlmInvocationService.h"
#include "Misc/UnrealAiEditorModalMonitor.h"
#include "Widgets/UnrealAiToolUi.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <atomic>

namespace UnrealAiAgentHarnessPriv
{
	static const TCHAR* GUnrealAiDispatchToolName = TEXT("unreal_ai_dispatch");

	static bool UnwrapDispatchToolCall(const FUnrealAiToolCallSpec& Tc, FString& OutInvokeName, FString& OutInvokeArgsJson, FString& OutError)
	{
		if (Tc.Name != GUnrealAiDispatchToolName)
		{
			OutInvokeName = Tc.Name;
			OutInvokeArgsJson = Tc.ArgumentsJson;
			OutError.Reset();
			return true;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Tc.ArgumentsJson);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("unreal_ai_dispatch: invalid JSON arguments");
			return false;
		}
		if (!Root->TryGetStringField(TEXT("tool_id"), OutInvokeName) || OutInvokeName.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("unreal_ai_dispatch: missing or empty tool_id");
			return false;
		}
		OutInvokeName.TrimStartAndEndInline();
		const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
		if (Root->TryGetObjectField(TEXT("arguments"), ArgsObjPtr) && ArgsObjPtr && (*ArgsObjPtr).IsValid())
		{
			FString Out;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			if (FJsonSerializer::Serialize((*ArgsObjPtr).ToSharedRef(), W))
			{
				OutInvokeArgsJson = Out;
			}
			else
			{
				OutInvokeArgsJson = TEXT("{}");
			}
		}
		else
		{
			FString ArgsStr;
			if (Root->TryGetStringField(TEXT("arguments"), ArgsStr))
			{
				OutInvokeArgsJson = ArgsStr;
			}
			else
			{
				OutInvokeArgsJson = TEXT("{}");
			}
		}
		OutError.Reset();
		return true;
	}

	static bool ShouldRetryTransientTransportError(const FString& Msg)
	{
		if (Msg.IsEmpty())
		{
			return false;
		}
		const bool bTransportFail = Msg.Contains(TEXT("HTTP request failed"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("Failed (Cancelled)"), ESearchCase::IgnoreCase);
		const bool bTransient = Msg.Contains(TEXT("Cancelled"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("ConnectionError"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("TimedOut"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("Timeout"), ESearchCase::IgnoreCase);
		return bTransportFail && bTransient;
	}

	static void MergeToolCallDeltas(TArray<FUnrealAiToolCallSpec>& Acc, const TArray<FUnrealAiToolCallSpec>& Delta)
	{
		for (const FUnrealAiToolCallSpec& D : Delta)
		{
			if (D.StreamMergeIndex >= 0)
			{
				while (Acc.Num() <= D.StreamMergeIndex)
				{
					Acc.AddDefaulted();
				}
				FUnrealAiToolCallSpec& Slot = Acc[D.StreamMergeIndex];
				if (!D.Id.IsEmpty())
				{
					Slot.Id = D.Id;
				}
				if (!D.Name.IsEmpty())
				{
					Slot.Name = D.Name;
				}
				Slot.ArgumentsJson += D.ArgumentsJson;
			}
			else
			{
				Acc.Add(D);
			}
		}
	}

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
		FUnrealAiUsageTracker* UsageTracker = nullptr;
		TUniquePtr<FUnrealAiConversationStore> Conv;

		FUnrealAiTokenUsage UsageThisRound;
		FUnrealAiTokenUsage AccumulatedUsage;

		FGuid RunId;
		int32 LlmRound = 0;
		/** From model profile `maxAgentLlmRounds` (default 32), set each DispatchLlm. */
		int32 EffectiveMaxLlmRounds = 32;
		/** Retries transient HTTP-level cancellations without consuming a round. */
		int32 TransientTransportRetryCountThisRound = 0;
		// Keep retries minimal so headed live runs fail fast instead of stalling for many minutes.
		static constexpr int32 MaxTransientTransportRetriesPerRound = 0;

		FString AssistantBuffer;
		TArray<FUnrealAiToolCallSpec> PendingToolCalls;
		bool bFinishSeen = false;
		std::atomic<bool> bCancelled{false};
		std::atomic<bool> bTerminal{false};

		void HandleEvent(const FUnrealAiLlmStreamEvent& Ev);
		void DispatchLlm(bool bRetrySameRound = false);
		void CompleteToolPath();
		void CompleteAssistantOnly();
		void Fail(const FString& Msg);
		void Succeed();
		void AccumulateRoundUsage();

		static constexpr int32 CharPerTokenApprox = 4;
	};

	void FAgentTurnRunner::Fail(const FString& Msg)
	{
		bool Expected = false;
		if (!bTerminal.compare_exchange_strong(Expected, true, std::memory_order_relaxed))
		{
			return;
		}
		UE_LOG(
			LogTemp,
			Display,
			TEXT("UnrealAi harness: runner_terminal_set result=failed msg=%s"),
			Msg.IsEmpty() ? TEXT("<none>") : *Msg);
		FUnrealAiEditorModalMonitor::NotifyAgentTurnEndedForSink(Sink);
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
		UE_LOG(LogTemp, Display, TEXT("UnrealAi harness: runner_terminal_set result=success"));
		if (Conv.IsValid())
		{
			Conv->SaveNow();
		}
		if (UsageTracker && Profiles && !Request.ModelProfileId.IsEmpty()
			&& (AccumulatedUsage.PromptTokens > 0 || AccumulatedUsage.CompletionTokens > 0
				|| AccumulatedUsage.TotalTokens > 0))
		{
			UsageTracker->RecordUsage(Request.ModelProfileId, AccumulatedUsage, *Profiles);
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
		FUnrealAiEditorModalMonitor::NotifyAgentTurnEndedForSink(Sink);
	}

	void FAgentTurnRunner::AccumulateRoundUsage()
	{
		int32 P = UsageThisRound.PromptTokens;
		int32 C = UsageThisRound.CompletionTokens;
		if (P == 0 && C == 0 && UsageThisRound.TotalTokens > 0)
		{
			P = UsageThisRound.TotalTokens / 2;
			C = UsageThisRound.TotalTokens - P;
		}
		AccumulatedUsage.PromptTokens += P;
		AccumulatedUsage.CompletionTokens += C;
		AccumulatedUsage.TotalTokens += UsageThisRound.TotalTokens;
		UsageThisRound = FUnrealAiTokenUsage();
	}

	void FAgentTurnRunner::DispatchLlm(bool bRetrySameRound)
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
		FUnrealAiModelCapabilities CapLimits;
		Profiles->GetEffectiveCapabilities(Request.ModelProfileId, CapLimits);
		int32 ParsedMax = CapLimits.MaxAgentLlmRounds > 0 ? CapLimits.MaxAgentLlmRounds : 32;
		{
			const FString MaxRoundsEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HARNESS_MAX_LLM_ROUNDS"));
			if (!MaxRoundsEnv.IsEmpty())
			{
				const int32 EnvMax = FCString::Atoi(*MaxRoundsEnv);
				if (EnvMax > 0)
				{
					ParsedMax = FMath::Min(ParsedMax, EnvMax);
				}
			}
		}
		if (Request.LlmRoundBudgetFloor > 0)
		{
			ParsedMax = FMath::Max(ParsedMax, Request.LlmRoundBudgetFloor);
		}
		EffectiveMaxLlmRounds = FMath::Clamp(ParsedMax, 1, 512);

		if (!bRetrySameRound)
		{
			if (LlmRound >= EffectiveMaxLlmRounds)
			{
				Fail(FString::Printf(
					TEXT("Max tool/LLM rounds exceeded (%d). Increase \"Max agent LLM rounds\" in AI Settings."),
					EffectiveMaxLlmRounds));
				return;
			}
			++LlmRound;
			TransientTransportRetryCountThisRound = 0;
			if (LlmRound > 1 && Sink.IsValid())
			{
				Sink->OnRunContinuation(LlmRound - 1, EffectiveMaxLlmRounds);
			}
		}
		UsageThisRound = FUnrealAiTokenUsage();
		AssistantBuffer.Reset();
		PendingToolCalls.Reset();
		bFinishSeen = false;

		FUnrealAiLlmRequest LlmReq;
		FString BuildErr;
		TArray<FString> ContextUserMsgs;
		if (!UnrealAiTurnLlmRequestBuilder::Build(
				Request,
				LlmRound,
				EffectiveMaxLlmRounds,
				ContextService,
				Profiles,
				Catalog,
				Conv.Get(),
				CharPerTokenApprox,
				LlmReq,
				ContextUserMsgs,
				BuildErr))
		{
			Fail(BuildErr);
			return;
		}
		if (Sink.IsValid() && ContextUserMsgs.Num() > 0)
		{
			Sink->OnContextUserMessages(ContextUserMsgs);
		}

		const TSharedPtr<FAgentTurnRunner> Self = AsShared();
		UE_LOG(LogTemp, Display,
			TEXT("UnrealAi harness: LLM round %d/%d — submitting HTTP request (stream=%s). "
				 "No viewport progress until tools execute."),
			LlmRound,
			EffectiveMaxLlmRounds,
			LlmReq.bStream ? TEXT("yes") : TEXT("no"));
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
			MergeToolCallDeltas(PendingToolCalls, Ev.ToolCalls);
			break;
		case EUnrealAiLlmStreamEventType::Finish:
			UsageThisRound.PromptTokens = FMath::Max(UsageThisRound.PromptTokens, Ev.Usage.PromptTokens);
			UsageThisRound.CompletionTokens = FMath::Max(UsageThisRound.CompletionTokens, Ev.Usage.CompletionTokens);
			UsageThisRound.TotalTokens = FMath::Max(UsageThisRound.TotalTokens, Ev.Usage.TotalTokens);
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
		{
			const FString ErrorMsg = Ev.ErrorMessage.IsEmpty() ? TEXT("LLM error") : Ev.ErrorMessage;
			if (!bCancelled.load(std::memory_order_relaxed)
				&& ShouldRetryTransientTransportError(ErrorMsg)
				&& TransientTransportRetryCountThisRound < MaxTransientTransportRetriesPerRound)
			{
				++TransientTransportRetryCountThisRound;
				DispatchLlm(true);
				break;
			}
			Fail(ErrorMsg);
			break;
		}
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
		PendingToolCalls.RemoveAll([](const FUnrealAiToolCallSpec& T)
		{
			return T.Name.TrimStartAndEnd().IsEmpty();
		});
		if (PendingToolCalls.Num() == 0)
		{
			Fail(TEXT("Model completed tool_calls but no valid tool name (empty function.name)"));
			return;
		}
		FString TodoPlanEffectiveName;
		if (PendingToolCalls.Num() == 1)
		{
			FString TmpArgs, UnwrapE;
			if (!UnwrapDispatchToolCall(PendingToolCalls[0], TodoPlanEffectiveName, TmpArgs, UnwrapE))
			{
				TodoPlanEffectiveName.Reset();
			}
		}
		const bool bTodoPlanOnly = PendingToolCalls.Num() == 1
			&& TodoPlanEffectiveName == TEXT("agent_emit_todo_plan");
		int32 ToolSuccessCount = 0;
		int32 ToolFailCount = 0;
		FUnrealAiConversationMessage Am;
		Am.Role = TEXT("assistant");
		Am.Content = AssistantBuffer;
		Am.ToolCalls = PendingToolCalls;
		Conv->GetMessagesMutable().Add(Am);

		for (const FUnrealAiToolCallSpec& Tc : PendingToolCalls)
		{
			FString InvokeName;
			FString InvokeArgs;
			FString UnwrapErr;
			if (!UnwrapDispatchToolCall(Tc, InvokeName, InvokeArgs, UnwrapErr))
			{
				if (Sink.IsValid())
				{
					Sink->OnToolCallStarted(Tc.Name, Tc.Id, Tc.ArgumentsJson);
				}
				const FString ModelToolContent = UnwrapErr;
				if (Sink.IsValid())
				{
					Sink->OnToolCallFinished(Tc.Name, Tc.Id, false, UnrealAiTruncateForUi(ModelToolContent), nullptr);
				}
				FUnrealAiConversationMessage Tm;
				Tm.Role = TEXT("tool");
				Tm.ToolCallId = Tc.Id;
				Tm.Content = ModelToolContent;
				Conv->GetMessagesMutable().Add(Tm);
				++ToolFailCount;
				continue;
			}
			if (Sink.IsValid())
			{
				Sink->OnToolCallStarted(InvokeName, Tc.Id, InvokeArgs);
			}
			const FUnrealAiToolInvocationResult Inv = Tools->InvokeTool(InvokeName, InvokeArgs, Tc.Id);
			const FString DialogFootnote = FUnrealAiEditorModalMonitor::ConsumePendingToolDialogFootnote();
			FString ModelToolContent = Inv.bOk ? Inv.ContentForModel : Inv.ErrorMessage;
			if (!DialogFootnote.IsEmpty())
			{
				if (!ModelToolContent.IsEmpty())
				{
					ModelToolContent += TEXT("\n");
				}
				ModelToolContent +=
					FString::Printf(TEXT("[Editor blocking dialog during tool]: %s"), *DialogFootnote);
			}
			if (Inv.bOk)
			{
				++ToolSuccessCount;
			}
			else
			{
				++ToolFailCount;
			}
			if (Sink.IsValid())
			{
				Sink->OnToolCallFinished(
					InvokeName,
					Tc.Id,
					Inv.bOk,
					UnrealAiTruncateForUi(ModelToolContent),
					Inv.EditorPresentation);
			}
			if (InvokeName == TEXT("agent_emit_todo_plan") && Sink.IsValid())
			{
				FString PlanTitle = TEXT("Plan");
				TSharedPtr<FJsonObject> ArgsObj;
				TSharedRef<TJsonReader<>> AR = TJsonReaderFactory<>::Create(InvokeArgs);
				if (FJsonSerializer::Deserialize(AR, ArgsObj) && ArgsObj.IsValid())
				{
					ArgsObj->TryGetStringField(TEXT("title"), PlanTitle);
				}
				const FString PlanBody = (Inv.bOk && Inv.ContentForModel.TrimStart().StartsWith(TEXT("{")))
					? Inv.ContentForModel
					: InvokeArgs;
				Sink->OnTodoPlanEmitted(PlanTitle, PlanBody);
			}
			FUnrealAiConversationMessage Tm;
			Tm.Role = TEXT("tool");
			Tm.ToolCallId = Tc.Id;
			Tm.Content = ModelToolContent;
			Conv->GetMessagesMutable().Add(Tm);
		}
		// The next round still runs (DispatchLlm below), but models often reply with text-only and end
		// the run. A synthetic user line nudges execution when the only tool was the todo plan.
		if (bTodoPlanOnly && Request.Mode != EUnrealAiAgentMode::Ask)
		{
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			Nudge.Content = TEXT(
				"[Harness] Plan recorded. Continue immediately: execute the first pending plan step using "
				"Unreal Editor tools. Do not finish with narration only—invoke tools this turn unless truly blocked.");
			Conv->GetMessagesMutable().Add(Nudge);
		}
		else if (Request.Mode == EUnrealAiAgentMode::Agent)
		{
			// Agent mode should keep iterating after tool execution. This avoids frequent one-tool stalls where
			// the next model turn summarizes and ends instead of taking the next concrete action.
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			Nudge.Content = FString::Printf(
				TEXT("[Harness] Tool round complete (ok=%d, failed=%d). Continue executing the user's request. ")
				TEXT("If the task is not complete, call the next tool now. Only finish when the requested scene/work is actually done or you are truly blocked."),
				ToolSuccessCount,
				ToolFailCount);
			Conv->GetMessagesMutable().Add(Nudge);
		}
		PendingToolCalls.Reset();
		AssistantBuffer.Reset();
		AccumulateRoundUsage();
		DispatchLlm();
	}

	void FAgentTurnRunner::CompleteAssistantOnly()
	{
		if (!Conv.IsValid())
		{
			Fail(TEXT("Conversation missing"));
			return;
		}
		AccumulateRoundUsage();
		FUnrealAiConversationMessage Am;
		Am.Role = TEXT("assistant");
		Am.Content = AssistantBuffer;
		Conv->GetMessagesMutable().Add(Am);

		// Second+ LLM rounds often end with finish_reason=stop and no tool_calls. Models sometimes return an
		// empty assistant message (streaming quirk or "done" signal) even though work is unfinished—treat
		// that as a stall, not a successful completion, and schedule another round while under the cap.
		const bool bModeWantsTools =
			(Request.Mode == EUnrealAiAgentMode::Agent || Request.Mode == EUnrealAiAgentMode::Orchestrate);
		if (bModeWantsTools && AssistantBuffer.TrimStartAndEnd().IsEmpty() && LlmRound < EffectiveMaxLlmRounds)
		{
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			Nudge.Content = TEXT(
				"[Harness] The model returned an empty assistant message. Continue the user's task: call the ")
				TEXT("next tool(s) now, or briefly explain what blocks you. Do not reply with an empty message.");
			Conv->GetMessagesMutable().Add(Nudge);
			AssistantBuffer.Reset();
			DispatchLlm();
			return;
		}

		Succeed();
	}
}

FUnrealAiAgentHarness::FUnrealAiAgentHarness(
	IUnrealAiPersistence* InPersistence,
	IAgentContextService* InContext,
	FUnrealAiModelProfileRegistry* InProfiles,
	FUnrealAiToolCatalog* InCatalog,
	TSharedPtr<ILlmTransport> InTransport,
	IToolExecutionHost* InToolHost,
	FUnrealAiUsageTracker* InUsageTracker)
	: Persistence(InPersistence)
	, Context(InContext)
	, Profiles(InProfiles)
	, Catalog(InCatalog)
	, Transport(MoveTemp(InTransport))
	, ToolHost(InToolHost)
	, UsageTracker(InUsageTracker)
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

	FUnrealAiEditorModalMonitor::NotifyAgentTurnStarted(Sink);

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
	Runner->UsageTracker = UsageTracker;
	Runner->AccumulatedUsage = FUnrealAiTokenUsage();
	Runner->Conv = MakeUnique<FUnrealAiConversationStore>(Persistence);
	Runner->Conv->LoadOrCreate(Request.ProjectId, Request.ThreadId);

	Context->LoadOrCreate(Request.ProjectId, Request.ThreadId);
	ToolHost->SetToolSession(Request.ProjectId, Request.ThreadId);

	Runner->RunId = FGuid::NewGuid();
	FUnrealAiRunIds Ids;
	Ids.RunId = Runner->RunId;
	Sink->OnRunStarted(Ids);

	// Chat naming is a first-turn-only instruction. We inject it into the LLM conversation,
	// but it is never shown in the chat UI because the UI transcript is driven by chat send/stream events.
	const bool bFirstUserMessageInThread = (Runner->Conv->GetMessages().Num() == 0);
	if (bFirstUserMessageInThread)
	{
		FUnrealAiConversationMessage NameInstr;
		NameInstr.Role = TEXT("system");
		NameInstr.Content = TEXT(
			"[Hidden] Chat naming:\n"
			"On your FIRST assistant reply in this chat, you MUST append exactly one token:\n"
			"<chat-name: \"<short name>\"> \n"
			"The chat name should be 3-8 words, human-friendly, and derived from the user's goal.\n"
			"Do not explain the token to the user. The application will strip the token from visible output.\n");
		Runner->Conv->GetMessagesMutable().Add(NameInstr);
	}

	FUnrealAiConversationMessage UserMsg;
	UserMsg.Role = TEXT("user");
	UserMsg.Content = Request.UserText;
	Runner->Conv->GetMessagesMutable().Add(UserMsg);

	Runner->DispatchLlm();
}
